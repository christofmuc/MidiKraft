/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"
#include "MidiProgramNumber.h"

namespace midikraft {

	class CustomProgramChangeCapability {
	public:
		virtual std::vector<juce::MidiMessage> createCustomProgramChangeMessages(MidiProgramNumber program) const = 0;
	};

}

