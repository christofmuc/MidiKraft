/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "MidiController.h"
#include "ProgressHandler.h"

namespace midikraft {

	enum class MidiLoopType {
		Note, Sysex
	};

	struct MidiLoop {
		std::string midiOutput;
		std::string midiInput;
		MidiLoopType type;
	};

	class LoopDetection {
	public:
		static std::vector<MidiLoop> detectLoops(std::weak_ptr<ProgressHandler> progressHandler);
	};


}