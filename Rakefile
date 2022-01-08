require 'bundler'
require 'yard'

Bundler::GemHelper.install_tasks

YARD::Rake::YardocTask.new do |t|
    t.files         = [ 'lib/**/*.rb', 'ext/chd.c' ]
    t.options       = [ '-m', 'markdown' ]
    t.stats_options = [ '--list-undoc' ]
end
