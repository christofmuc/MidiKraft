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
		virtual std::vector<MidiMessage> requestStreamDump(int number) const = 0;
		virtual bool isPartOfStreamDump(const MidiMessage& message) const = 0;
		virtual bool isStreamDumpFinished(std::vector<MidiMessage> const &streamDump) const = 0;
		virtual TPatchVector loadStreamDump(std::vector<MidiMessage> const &streamDump) const = 0;
	};

}
