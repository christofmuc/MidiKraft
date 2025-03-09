/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "SynthParameterDefinition.h"

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
	};

	struct ParamDef {
		int param_id;
		std::string name;
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
		virtual std::vector<ParamDef> getParameterDefinitions() const = 0;

		virtual std::vector<ParamVal> getParameterValues(std::shared_ptr<DataFile> const patch, bool onlyActive) const = 0;

		// For clustering/auto-categorization and similarity search.
		// This is allowed to drop out parameters not considered relevant, and should convert list parameters to 
		// vector parameters. Must return always vectors of the same length.
		virtual std::vector<float> createFeatureVector(std::shared_ptr<DataFile> const patch) const = 0;

	};

}
