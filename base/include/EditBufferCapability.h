#pragma once

#include "JuceHeader.h"

#include "Patch.h"

class EditBufferCapability {
public:
	virtual MidiMessage requestEditBufferDump() = 0;
	virtual bool isEditBufferDump(const MidiMessage& message) const = 0;

	virtual std::shared_ptr<Patch> patchFromSysex(const MidiMessage& message) const = 0;
	virtual std::vector<MidiMessage> patchToSysex(const Patch &patch) const = 0;

	virtual MidiMessage saveEditBufferToProgram(int programNumber) = 0;
};