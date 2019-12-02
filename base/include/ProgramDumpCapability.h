#pragma once

#include "JuceHeader.h"

#include "Patch.h"

class ProgramDumpCabability {
public:
	virtual std::vector<MidiMessage> requestPatch(int patchNo) = 0;
	virtual bool isSingleProgramDump(const MidiMessage& message) const = 0;
	virtual std::shared_ptr<Patch> patchFromProgramDumpSysex(const MidiMessage& message) const = 0;
	virtual std::vector<MidiMessage> patchToProgramDumpSysex(const Patch &patch) const = 0;	
};
