#pragma once

#include "Synth.h"
#include "PatchNumber.h"

class SynthParameterDefinition;

class Patch {
public:
	Patch();
	Patch(Synth::PatchData const &patchdata);
	virtual std::string patchName() const = 0;
	virtual void setName(std::string const &name) = 0;

	virtual std::shared_ptr<PatchNumber> patchNumber() const = 0;
	virtual void setPatchNumber(MidiProgramNumber patchNumber) = 0;
	
	// Direct byte access functions
	virtual void setData(Synth::PatchData const &data);
	virtual Synth::PatchData const &data() const;
	virtual int at(int sysExIndex) const;
	virtual void setAt(int sysExIndex, uint8 value);

	// High level functions to be overridden by subclasses
	virtual int value(SynthParameterDefinition const &param) const = 0;
	virtual SynthParameterDefinition const &paramBySysexIndex(int sysexIndex) const = 0;
	virtual std::vector<std::string> warnings() = 0;
	virtual std::vector<SynthParameterDefinition *> allParameterDefinitions() = 0;

	// Generic utility functions
	std::string patchToTextRaw(bool onlyActive);

	// For patch comparison
	static Synth::PatchData blankOut(std::vector<Range<int>> const &blankoutZones, Synth::PatchData const &inputData);

private:
	// Direct byte storage
	Synth::PatchData data_;
};

