/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Patch.h"
#include "MidiProgramNumber.h"

namespace midikraft {

	class ProgramDumpCabability {
	public:
		virtual std::vector<MidiMessage> requestPatch(int patchNo) const = 0;
		virtual bool isSingleProgramDump(const MidiMessage& message) const = 0;
		virtual MidiProgramNumber getProgramNumber(const MidiMessage &message) const = 0;
		virtual std::shared_ptr<DataFile> patchFromProgramDumpSysex(const MidiMessage& message) const = 0;
		virtual std::vector<MidiMessage> patchToProgramDumpSysex(std::shared_ptr<DataFile> patch, MidiProgramNumber programNumber) const = 0;
	};

	class DefaultProgramPlaceInsteadOfEditBufferCapability {
	public:
		virtual MidiProgramNumber getDefaultProgramPlace() const = 0;
	};
}
