This library provide access to MAME CHD file format, using the [libchr][1]
library, If the library is not available on the system, the gem will 
used its bundled version.


Examples
========

~~~ruby
CHD.open('file.chd') do |chd|
    puts chd.version
	puts chd.header
	puts chd.metadata	
	
	chd.read_hunk(0)
	chd.read_unit(0)
	chd.read_bytes(1234, 5678)
end
~~~

~~~ruby
chd = CHD.new('file.chd')
cd  = CHD::CD.new(chd)
cd.read_sector(1, :MODE1)
~~~



[1]: https://github.com/rtissera/libchdr

