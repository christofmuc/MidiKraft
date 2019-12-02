#pragma once

#include "MidiProgramNumber.h"

#include <string>

class PatchNumber {
public:
	PatchNumber() : programNumber_(MidiProgramNumber::fromZeroBase(0)) {}
	PatchNumber(MidiProgramNumber const &programNumber) : programNumber_(programNumber) { };
	virtual ~PatchNumber() = default;

	virtual std::string friendlyName() const = 0;

	MidiProgramNumber midiProgramNumber() const { return programNumber_; }

protected:
	MidiProgramNumber programNumber_;
};

