/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Patch.h"

namespace midikraft {

	class ProgramDumpCabability {
	public:
		virtual std::vector<MidiMessage> requestPatch(int patchNo) = 0;
		virtual bool isSingleProgramDump(const MidiMessage& message) const = 0;
		virtual std::shared_ptr<Patch> patchFromProgramDumpSysex(const MidiMessage& message) const = 0;
		virtual std::vector<MidiMessage> patchToProgramDumpSysex(const Patch &patch) const = 0;
	};

}
