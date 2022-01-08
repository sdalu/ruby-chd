require 'mkmf'
require 'set'

ROOT = File.join(__dir__, '..')

def with_success(&block)
    state = [ $CFLAGS, $LDFLAGS, $libs ]
    block.call.tap {|success|
        $CFLAGS, $LDFLAGS, $libs = state unless success
    }
end


build = Set[]

cmake = find_executable('cmake')

unless pkg_config('libchdr').tap {|found|
           puts "found pkg-config for libchdr" if found
       } ||
       with_success do
           find_library('chdr', 'chd_open') & find_header('libchdr/chd.h')
       end

    $CFLAGS += " -I " + File.join(ROOT, 'libchdr', 'include')

    $libs += [ 'deps/zlib-1.2.11/libzlib.a',
               'deps/lzma-19.00/liblzma.a',
               'CMakeFiles/chdr.dir/src/libchdr_chd.c.o',
               'CMakeFiles/chdr.dir/src/libchdr_flac.c.o',
               'CMakeFiles/chdr.dir/src/libchdr_bitstream.c.o',
               'CMakeFiles/chdr.dir/src/libchdr_huffman.c.o',
               'CMakeFiles/chdr.dir/src/libchdr_cdrom.c.o',
             ].map {|l|
        ' ' + File.join(ROOT, 'build', l)
    }.join

#    $libs   += [ 'libchdr-static.a', 'liblzma.a', 'libzlib.a' ].map {|l|
#        ' ' + File.join(ROOT, 'stage', 'lib', l)
#    }.join(' ')

    build.add(:libchdr)
end

create_makefile('chd/core')


if build.include?(:libchdr)
    puts "Building bundled libchdr"

    Dir.chdir(ROOT) do
        unless cmake                                                    &&
               system(cmake, '-DINSTALL_STATIC_LIBS=on',
                             '-S', 'libchdr', '-B', 'build')            &&
               system(cmake, '--build',   'build')
            abort "failed building libchdr"
        end
    end
end

