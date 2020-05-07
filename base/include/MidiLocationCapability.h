#pragma once

#include "JuceHeader.h"

#include "MidiChannel.h"

namespace midikraft {

	class MidiLocationCapability {
	public:
		virtual MidiChannel channel() const = 0;
		virtual std::string midiInput() const = 0;
		virtual std::string midiOutput() const = 0;
	};

}

