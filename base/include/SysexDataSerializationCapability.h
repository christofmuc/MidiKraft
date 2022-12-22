/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

namespace midikraft {

	class SysexDataSerializationCapability {
	public:
		virtual bool dataFromSysex(const std::vector<MidiMessage> &message) = 0;
		virtual std::vector<MidiMessage> dataToSysex() const = 0;
	};

}
