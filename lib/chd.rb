# coding: utf-8
require 'chd/core'
require 'chd/metadata'
require 'chd/cd'

class CHD

    # Returns a string representation of the number of frames using
    # minutes:seconds:frames format.
    #
    # @param frames [Integer]
    #
    # @return [String]
    #
    def self.msf(frames)
        "%02d:%02d:%02d" % [ frames / (75 * 60), (frames / 75) % 60, frames % 75 ]
    end
end



