/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Patch.h"

namespace midikraft {

	class StreamDumpCapability {
	public:
		virtual MidiMessage requestDump(int number) const = 0;
		virtual bool isPartOfDump(const MidiMessage& message) const = 0;
		virtual bool isDumpFinished(std::vector<MidiMessage> const &bankDump) const = 0;
	};

}
