#pragma once

#include "JuceHeader.h"

class SysexDataSerializationCapability {
public:
	virtual bool dataFromSysex(const std::vector<MidiMessage> &message) = 0;
	virtual std::vector<MidiMessage> dataToSysex() const = 0;
};

