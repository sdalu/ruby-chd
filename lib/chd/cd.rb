require 'set'
require 'digest'

class CHD

#
# Access a CD-ROM / GD-ROM in Mame CHD format
#
class CD
    # Maximum number of tracks in a CD-ROM
    MAX_TRACKS             = 99

    # Maximum sector size
    MAX_SECTOR_DATASIZE    = 2352

    # Maximum subcode size
    MAX_SUBCODE_DATASIZE   = 96

    # Maximum frame size
    FRAME_SIZE             = MAX_SUBCODE_DATASIZE + MAX_SECTOR_DATASIZE

    # @!visibility private
    TRACK_PADDING          = 4

    # Various sector data size according to track type
    TRACK_TYPE_DATASIZE    = {
         :MODE1          => 2048,
         :MODE1_RAW      => 2352,
         :MODE2          => 2336,
         :MODE2_FORM1    => 2048,
         :MODE2_FORM2    => 2324,
         :MODE2_FORM_MIX => 2336,
         :MODE2_RAW      => 2352,
         :AUDIO          => 2352,
    }.freeze
    
    # Various subcode data size according to track type
    TRACK_SUBTYPE_DATASIZE = {
        :NONE            => 0,
        :NORMAL          => 96,
        :RAW             => 96,
    }.freeze

    private
    
    SYNCBYTES              = [ 0x00, 0xff, 0xff, 0xff, 0xff, 0xff,
                               0xff, 0xff, 0xff, 0xff, 0xff, 0x00
                             ].pack('C*').freeze
    
    public

    # Read the TOC, and returns it's information in a parsed form.
    #
    # @param chd [CHD] a chd opened file
    #
    # @return [Array<Hash{Symbol => Object}>] Table of Content
    # @return [nil] if the CHD file is not of a CD-ROM / GD-ROM type
    #
    def self.read_toc(chd)
        return nil if chd.hunk_bytes %  FRAME_SIZE != 0 ||
                      chd.unit_bytes != FRAME_SIZE
        
        flags  = Set.new
        tracks = []
        while (idx = tracks.size) < MAX_TRACKS do
	    if    md = chd.get_metadata(idx, Metadata::CDROM_TRACK,      )
	    elsif md = chd.get_metadata(idx, Metadata::CDROM_TRACK_PREGAP)
	    elsif md = chd.get_metadata(idx, Metadata::GDROM_OLD,        )
                raise NotSupported, "upgrade your CHD to a more recent version"
	    elsif md = chd.get_metadata(idx, Metadata::GDROM_TRACK,      )
                flags << :GDROM
	    else
	        break            
	    end
            tracks << Metadata.parse(*md)
        end
        
        if ! tracks.empty?
            unless tracks.each_with_index.all? {|trackinfo, index|
                       trackinfo[:track] == index + 1
                   }
                raise ParsingError, "unordered tracks"
            end
            [ tracks, flags ]
        elsif chd.get_metadata(0, Metadata::CDROM_OLD)
            raise NotSupported, "upgrade your CHD to a more recent version"
        else
            raise NotFoundError, "provided CHD is not a CD-ROM"
        end
    end
            
    
    def initialize(chd)
        @chd         = chd
        @toc, @flags = CD.read_toc(chd)

        # Build mapping 
	chdofs = physofs = logofs = 0
        @mapping = @toc.map {|trackinfo|
            { :physframeofs => physofs,
	      :chdframeofs  => chdofs,
	      :logframeofs  => trackinfo[:pregap] + logofs,
	      :logframes    => trackinfo[:frames] - trackinfo[:pregap],
            }.tap {
                logofs  += trackinfo[:pregap] if trackinfo[:pgdatasize].zero?
	        logofs  += trackinfo[:frames] + trackinfo[:postgap]
	        physofs += trackinfo[:frames]
                chdofs  += trackinfo[:frames] + trackinfo[:extraframes]
            }
        }
        @mapping <<  { :physframeofs => physofs,
                       :logframeofs  => logofs,
                       :chdframeofs  => chdofs,
                       :logframes    => 0,
                     }
    end

    
    # Table of Content
    #
    # @return [Array<Hash{Symbol => Object}>]
    #
    attr_reader :toc 

    # Get the frame number that the track starts at.
    #
    # @param track [Integer] track number (start at 1)
    #
    # @return [Integer] frame number
    #
    def track_start(track, phys = false)
        frame_ofs_type = phys ? :physframeofs : :logframeofs
        
	# handle lead-out specially
	if track == 0xAA
	    @mapping.last[frame_ofs_type]
        elsif ! (1 .. @toc.size).include?(track)
            raise RangeError, "track must be in 1..#{@toc.size}"
        else
            @mappging.dig(track - 1, frame_ofs_type)
        end
    end

    # Read one or more sectors from a CD-ROM
    #
    # @param lbasector [Integer] sector number
    # @param datatype  [Symbol]  type of data 
    #
    # @return [String]
    #
    def read_sector(lbasector, datatype = nil, phys = false)
        # Compute CHD sector and track index
        frame_ofs_type = phys ? :physframeofs : :logframeofs
        chdsector      = lbasector
        trackidx       = 0
        @mapping.each_cons(2).with_index do |(cur, nxt), idx|
            if lbasector < nxt[frame_ofs_type]
                chdsector = lbasector - cur[frame_ofs_type] + cur[:chdframeofs]
                trackidx = idx
                break
            end
        end


        trackinfo = @toc[trackidx];
	tracktype = trackinfo[:trktype]

	offset, length, header =
             # return same type or don't care
             if (datatype == tracktype) || datatype.nil?
                 [ 0,  trackinfo[:datasize] ]
                 
	     # return 2048 bytes of MODE1 data
             #   from a 2352 byte MODE1 RAW sector
	     elsif (datatype  == :MODE1    ) &&
                   (tracktype == :MODE1_RAW)
	         [ 16, 2048 ]
                 
	     # return 2352 byte MODE1 RAW sector
             #  from 2048 bytes of MODE1 data
	     elsif (datatype  == :MODE1_RAW) &&
                   (tracktype == :MODE1    )
	         warn "promotion of MODE1/FORM1 sector to MODE1 RAW is incomplete"
                 m, sf = lba.divmod(60 * 75);
                 s, f  = sf.divmod(75)
                 hdr   = SYNCBYTES + [
	             ((m / 10) << 4) | ((m % 10) << 0), # M
	             ((s / 10) << 4) | ((s % 10) << 0), # S
	             ((f / 10) << 4) | ((f % 10) << 0), # F
                     1                                  # MODE1
                 ].pack('C*') # MSF + MODE1
                 
                 [ 0, 2048, hdr ]
                 
	     # return 2048 bytes of MODE1 data
             #   from a MODE2 FORM1 or RAW sector
             elsif (datatype  == :MODE1      ) &&
                  ((tracktype == :MODE2_FORM1) || (tracktype == :MODE2_RAW  ))
	         [ 24, 2048 ]
                 
	     # return 2048 bytes of MODE1 data
             #   from a MODE2 FORM2 or XA sector
	     elsif (datatype  == :MODE1         ) &&
                   (tracktype == :MODE2_FORM_MIX)
	         [  8, 2048 ]
                 
             # return MODE2 2336 byte data
             #   from a 2352 byte MODE1 or MODE2 RAW sector (skip the header)
	     elsif (datatype  == :MODE2) &&
                  ((tracktype == :MODE1_RAW) || (tracktype == :MODE2_RAW))
	         [ 16, 2336 ]
                 
             # Not supported
             else
                 raise NotSupported,
                       "conversion from type %s to type %s not supported" % [
                           tracktype, datatype ]
	     end

        # Read data
	unless phys 
	    if ! trackinfo[:pgdatasize].zero?
		# chdman (phys=true) relies on chdframeofs to point
                # to index 0 instead of index 1 for extractcd.
		# Actually playing CDs requires it to point
                # to index 1 instead of index 0,
                # so adjust the offset when phys=false.
		chdsector += trackinfo[:pregap]
            elsif  lbasector < trackinfo[:logframeofs]
	        # if this is pregap info that isn't actually in the file,
                # just return blank data
                return '\0' * length
	    end
	end

        data = @chd.read_bytes(chdsector * FRAME_SIZE + offset, length)
        data = header + data if header
        data
    end
 

    private
    

    def _enhance_toc(toc)
	chd_offset = physical_offset = logical_offset = 0

        toc.each do |trackinfo|
	    trackinfo[:logframeofs] = 0
            
	    if trackinfo[:pgdatasize].zero?
		logical_offset         += trackinfo[:pregap]
	    else
		trackinfo[:logframeofs] = trackinfo[:pregap]
            end

	    trackinfo[:physframeofs] = physical_offset
	    trackinfo[:chdframeofs ] = chd_offset
	    trackinfo[:logframeofs ] += logical_offset
	    trackinfo[:logframes   ] = trackinfo[:frames] - trackinfo[:pregap]

	    logical_offset  += trackinfo[:frames] + trackinfo[:postgap]
	    physical_offset += trackinfo[:frames]
            chd_offset      += trackinfo[:frames] + trackinfo[:extraframes]
        end

        toc
    end

end
end
        
