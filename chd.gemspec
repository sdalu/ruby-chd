# -*- encoding: utf-8 -*-

require_relative 'lib/chd/version'

Gem::Specification.new do |s|
    s.name        = 'chd'
    s.version     = CHD::VERSION
    s.summary     = "CHD Ruby API"
    s.description =  <<~EOF
      
      Open MAME CHD file.

      EOF

    s.homepage    = 'https://github.com/sdalu/ruby-chd'
    s.license     = 'MIT'

    s.authors     = [ "Stéphane D'Alu" ]
    s.email       = [ 'sdalu@sdalu.com' ]

    s.extensions  = [ "ext/extconf.rb" ]
    s.files       = %w[ README.md chd.gemspec ] 			+
		    Dir['ext/**/*.{c,h,rb}'] 				+
		    Dir['libchdr/**/*'] 				+
                    Dir['lib/**/*.rb']

    s.add_development_dependency 'yard', '~>0'
    s.add_development_dependency 'rake', '~>13'
end
