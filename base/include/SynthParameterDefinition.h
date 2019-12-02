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

	class SynthParameterDefinition {
	public:
		virtual std::string name() const = 0;
		virtual std::string valueAsText(int value) const = 0;
		virtual int sysexIndex() const = 0;
		virtual std::string description() const = 0;
		virtual bool matchesController(int controllerNumber) const = 0;
		virtual int minValue() const = 0;
		virtual int maxValue() const = 0;


		//! A parameter is deemed active when its value can have any meaningful effect on the sound produced
		virtual bool isActive(Patch const *patch) const = 0;

		virtual bool valueInPatch(Patch const &patch, int &outValue) const = 0;
	};

}
