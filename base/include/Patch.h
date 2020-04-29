/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "Synth.h"
#include "PatchNumber.h"

namespace midikraft {

	class SynthParameterDefinition;

	class StoredPatchNameCapability {
	public:
		virtual void setName(std::string const &name) = 0;
		virtual bool isDefaultName() const = 0;
	};

	class DataFile {
	public:
		DataFile(int dataTypeID);
		DataFile(int dataTypeID, Synth::PatchData const &patchdata);

		int dataTypeID() const;

		virtual std::string name() const = 0;

		// Direct byte access functions
		virtual void setData(Synth::PatchData const &data);
		virtual Synth::PatchData const &data() const;
		virtual int at(int sysExIndex) const;
		virtual void setAt(int sysExIndex, uint8 value);

	protected:
		// Just any ID you want to give it
		int dataTypeID_;

		// Direct byte storage
		Synth::PatchData data_;
	};

	class Patch : public DataFile {
	public:
		using DataFile::DataFile;

		virtual std::shared_ptr<PatchNumber> patchNumber() const = 0;
		virtual void setPatchNumber(MidiProgramNumber patchNumber) = 0;

		// High level functions to be overridden by subclasses
		virtual std::vector<std::shared_ptr<SynthParameterDefinition>> allParameterDefinitions() = 0;

		// Generic utility functions
		std::string patchToTextRaw(bool onlyActive);

		// For patch comparison
		static Synth::PatchData blankOut(std::vector<Range<int>> const &blankoutZones, Synth::PatchData const &inputData);
	};

}
