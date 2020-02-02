/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

namespace midikraft {

	class StreamLoadCapability {
	public:
		virtual bool isMessagePartOfStream(MidiMessage const &message) = 0;
		virtual bool isStreamComplete(std::vector<MidiMessage> const &messages) = 0;
		virtual bool shouldStreamAdvance(std::vector<MidiMessage> const &messages) = 0;
	};

}
