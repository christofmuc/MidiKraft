#pragma once

#include "JuceHeader.h"

#include "MidiChannel.h"

namespace midikraft {

	class MidiLocationCapability {
	public:
		virtual MidiChannel channel() const = 0;
		virtual juce::MidiDeviceInfo midiInput() const = 0;
		virtual juce::MidiDeviceInfo midiOutput() const = 0;
	};

}

