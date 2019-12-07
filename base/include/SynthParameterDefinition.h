/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include <map>

namespace midikraft {

	typedef std::map<int, std::string> TValueLookup;

	class Patch;
	class Synth;

	class SynthParameterDefinition {
	public:
		//TODO - should I use inheritance? Or a capability interface again?
		enum class ParamType {
			INT,
			INT_ARRAY,
			LOOKUP
		};

		// Synth parameters are usually integers, or arrays of integers (e.g. gated sequencer)
		virtual ParamType type() const = 0;

		virtual std::string name() const = 0; // Short name
		virtual std::string description() const = 0; // Long name / descriptive name

		virtual std::string valueAsText(int value) const = 0;

		virtual int sysexIndex() const = 0;
		virtual int endSysexIndex() const = 0;

		virtual bool matchesController(int controllerNumber) const = 0;
		virtual int minValue() const = 0;
		virtual int maxValue() const = 0;

		//! A parameter is deemed active when its value can have any meaningful effect on the sound produced
		virtual bool isActive(Patch const *patch) const = 0;

		//! Use this function to retrieve the integer value in the patch
		virtual bool valueInPatch(Patch const &patch, int &outValue) const = 0;
		//! Use this function to retrieve the integer array value in the patch
		virtual bool valueInPatch(Patch const &patch, std::vector<int> &outValue) const = 0;

		virtual std::string valueInPatchToText(Patch const &patch) const = 0;

		virtual void setInPatch(Patch &patch, int value) const = 0;
		virtual void setInPatch(Patch &patch, std::vector<int> value) const = 0;

		// This is for live editing!
		virtual MidiBuffer setValueMessage(Patch const &patch, Synth *synth) const = 0;
	};

}
