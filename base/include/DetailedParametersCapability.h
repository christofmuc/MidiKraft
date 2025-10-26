/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "SynthParameterDefinition.h"
#include "MidiChannel.h"


namespace midikraft {

	// Old class 
	class DetailedParametersCapability {
	public:
		virtual std::vector<std::shared_ptr<SynthParameterDefinition>> allParameterDefinitions() const = 0;
	};

	enum ParamType {
		VALUE = 0,
		CHOICE = 1,
		LIST = 2,
		CHOICE_LIST = 3
	};

	struct ParamDef {
		int param_id;
		std::string name;
		std::string description;
		ParamType param_type;
		juce::var values;
		std::optional<int> cc_number; // If a CC number is assigned to this parameter
		std::optional<int> nrpn_number; // If an NRPN number is assigned to this parameter
	};

	struct ParamVal {
		int param_id;
		juce::var value;
	};

	// New class, all in one, for Python interface
	class SynthParametersCapability {
	public:
		// The following two functions are required to be implemented by any Synth supporting the new SynthParametersCapability
		// They will be used by the Librarian to display a clear text description of the patch's parameters instead of just the hex dump
		virtual std::vector<ParamDef> getParameterDefinitions() const = 0;
		virtual std::vector<ParamVal> getParameterValues(std::shared_ptr<DataFile> const patch, bool onlyActive) const = 0;
		
		// Optionally, allow the software to set individual parameters in the patch using the param_id and a new value
		// Implementations may mutate the provided DataFile in-place or replace its contents entirely.
		// Returning true indicates that the patch now reflects the provided values.
		virtual bool setParameterValues(std::shared_ptr<DataFile> patch, std::vector<ParamVal> const &new_values) const = 0;

		// Use this to create individual parameter change messages to send to the synth, e.g. for an editor
		virtual std::vector<MidiMessage> createSetValueMessages(MidiChannel const channel, std::shared_ptr<DataFile> const patch, std::vector<int> param_ids) const = 0;

		// For clustering/auto-categorization and similarity search.
		// This is allowed to drop out parameters not considered relevant, and should convert list parameters to 
		// vector parameters. Must return always vectors of the same length.
		virtual std::vector<float> createFeatureVector(std::shared_ptr<DataFile> const patch) const = 0;
	};

}
