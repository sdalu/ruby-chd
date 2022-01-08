require 'set'

class CHD

#
# Parsing of the metadata failed.
#
class ParsingError < Error
end

#
#
# | Type | Associated scanf string                                                                |
# |------|----------------------------------------------------------------------------------------|
# | GDDD | "CYLS:%d,HEADS:%d,SECS:%d,BPS:%d"                                                      |
# | CHTR | "TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d"                                                |
# | CHT2 | "TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d PREGAP:%d PGTYPE:%s PGSUB:%s POSTGAP:%d"        |
# | CHGD | "TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d PAD:%d PREGAP:%d PGTYPE:%s PGSUB:%s POSTGAP:%d" |
# | AVAV | "FPS:%d.%06d WIDTH:%d HEIGHT:%d INTERLACED:%d CHANNELS:%d SAMPLERATE:%d"               |
#
module Metadata
    HARD_DISK                = :'GDDD'  # Hard disk
    HARD_DISK_IDENT          = :'IDNT'  # Hard disk identity
    HARD_DISK_KEY            = :'KEY '  # Hard disk key
    PCMCIA_CIS               = :'CIS '  # PCMCIA CIS
    CDROM_TRACK              = :'CHTR'  # CDROM Track
    CDROM_TRACK_PREGAP       = :'CHT2'  # CDROM Track with pregap
    GDROM_TRACK              = :'CHGD'  # GDROM Track (SEGA Genesis)
    AV                       = :'AVAV'  # Audio/Video
    AV_LD                    = :'AVLD'  # Audio/Video Laser Disk
    CDROM_OLD                = :'CHCD'  # CDROM Track (old CHD version)
    GDROM_OLD                = :'CHGT'  # GDROM Track (old CHD version)

    private
    
    HARD_DISK_REGEX          = /\A CYLS:      (?<cyls>%d)        ,
                                   HEADS:     (?<heads>%d)       ,
                                   SECS:      (?<secs>%d)        ,
                                   BPS:       (?<bps>)%d
                                \z /x
    CDROM_TRACK_REGEX        = /\A TRACK:     (?<track>\d+)      \s+
                                   TYPE:      (?<trktype>\w+)    \s+
                                   SUBTYPE:   (?<subtype>\w+)    \s+
                                   FRAMES:    (?<frames>\d+)
                                \z /x
    CDROM_TRACK_PREGAP_REGEX = /\A TRACK:     (?<track>\d+)      \s+
                                   TYPE:      (?<trktype>\w+)    \s+
                                   SUBTYPE:   (?<subtype>\w+)    \s+
                                   FRAMES:    (?<frames>\d+)     \s+
                                   PREGAP:    (?<pregap>\d+)     \s+
                                   PGTYPE:  V?(?<pgtype>\w+)     \s+
                                   PGSUB:     (?<pgsub>\w+)      \s+
                                   POSTGAP:   (?<postgap>\d+)
                                \z /x
    GDROM_TRACK_REGEX        = /\A TRACK:     (?<track>\d+)      \s+
                                   TYPE:      (?<trktype>\w+)    \s+
                                   SUBTYPE:   (?<subtype>\w+)    \s+
                                   FRAMES:    (?<frames>\d+)     \s+
                                   PAD:       (?<padframes>\d+)  \s+
                                   PREGAP:    (?<pregap>\d+)     \s+
                                   PGTYPE:  V?(?<pgtype>\w+)     \s+
                                   PGSUB:     (?<pgsub>\w+)      \s+
                                   POSTGAP:   (?<postgap>\d+)
                                \z /x
    AV_REGEX                 = /\A FPS:       (?<fps>\d+\.\d+)   \s+
                                   WIDTH:     (?<width>\d+)      \s+
                                   HEIGHT:    (?<height>\d+)     \s+
                                   INTERLACED:(?<interlaced>\d+) \s+
                                   CHANNELS:  (?<channels>\d+)   \s+
                                   SAMPLERATE:(?<samplerate>\d+)
                                \z /x


    CD_TRACK_TYPES = {
        'MODE1'          => :MODE1,
        'MODE1/2048'     => :MODE1,
        'MODE1_RAW'      => :MODE1_RAW,
        'MODE1/2352'     => :MODE1_RAW,
        'MODE2'          => :MODE2,
        'MODE2/2336'     => :MODE2,
        'MODE2_FORM1'    => :MODE2_FORM1,
        'MODE2/2048'     => :MODE2_FORM1,
        'MODE2_FORM2'    => :MODE2_FORM2,
        'MODE2/2324'     => :MODE2_FORM2,
        'MODE2_FORM_MIX' => :MODE2_FORM_MIX,
        'MODE2/2336'     => :MODE2_FORM_MIX,
        'MODE2_RAW'      => :MODE2_RAW,
        'MODE2/2352'     => :MODE2_RAW,
        'AUDIO'          => :AUDIO,
    }

    CD_TRACK_SUBTYPES = {
        'NONE'           => :NONE,
        'RW'             => :NORMAL,
        'RW_RAW'         => :RAW, 
    }



    public

    # Parse metadata returned by {CHD#get_metadata}
    #
    # @param data  [String]
    # @param flags [Integer]
    # @param type  [Symbol]
    #
    # @return [Hash{Symbol => Object}]
    #
    # @example
    #  chd = CHD.new('file.chd')
    #  puts CHD::Metadata.parse(*chd.get_metadata(0)).inspect
    #
    def self.parse(data, flags, type)
        # Flags
        unless (flags & ~(METADATA_FLAG_CHECKSUM)).zero?
            raise ParsingError,
                  "unsupported flag (0x#{flags.to_s(16)}) (fill bug report)"
        end
        set_flags = Set.new
        set_flags << :checksum unless (flags & METADATA_FLAG_CHECKSUM).zero?

        # Data
        case type
        when CDROM_TRACK        then parse_cdrom_track(data, CDROM_TRACK_REGEX)
        when CDROM_TRACK_PREGAP then parse_cdrom_track(data, CDROM_TRACK_PREGAP_REGEX)
        when GDROM_TRACK        then parse_cdrom_track(data, GDROM_TRACK_REGEX)
        when HARD_DISK          then parse_hard_disk(data, HARD_DISK_REGEX)
        else                    raise "not implemented yet"
        end
    end

    private

    def self.parse_cdrom_track(data, regex)
        md = parse_using_regex(data, regex, :trktype => CD_TRACK_TYPES,
                                            :subtype => CD_TRACK_SUBTYPES,
                                            :pgtype  => CD_TRACK_TYPES,
                                            :pgsub   => CD_TRACK_SUBTYPES)
        dflt = { :track     => nil,
                 :trktype   => nil,
                 :subtype   => nil,
                 :frames    => nil,
                 :padframes => 0,
                 :pregap    => 0,
                 :pgtype    => :MODE1,
                 :pgsub     => :NONE,
                 :postgap   => 0,
               }
       
        md = dflt.merge(md)
        
        if (md[:track] < 0) || (md[:track] > CD::MAX_TRACKS)
            raise ParsingError, "track number out of range"
        end
        
        md.merge(:extraframes => CD::TRACK_PADDING -
                                   md[:frames] % CD::TRACK_PADDING,
                 :datasize    => CD::TRACK_TYPE_DATASIZE[   md[:trktype]],
                 :subsize     => CD::TRACK_SUBTYPE_DATASIZE[md[:subtype]],
                 :pgdatasize  => CD::TRACK_TYPE_DATASIZE[   md[:pgtype ]],
                 :pgsubsize   => CD::TRACK_SUBTYPE_DATASIZE[md[:pgsub  ]],
                )
    end

    def self.parse_hard_disk(data, regex)
        parse_using_regex(data, regex)
    end

    def self.parse_using_regex(data, regex, mapping = nil, &block)
        unless md = regex.match(data)&.named_captures
            raise ParsingError, "failed to parse track"
        end

        md.transform_keys!(&:to_sym)
        md.transform_values! do |v|
            case v
            when /^\d+$/      then Integer(v)
            when /^\d+\.\d+$/ then Float(v)
            else v
            end
        end

        (mapping || {}).each do |key, map|
            md[key] = map.fetch(md[key]) if md.include?(key)
        end

        block&.call(md)

        md
    rescue KeyError
        raise ParsingError, "unable to decode track description"
    end
    
end
end
