/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

namespace midikraft {

	enum class ValueType {
		Integer,
		Bool,
		Lookup
	};

	struct  TypedNamedValue {
		String name;
		String sectionName;
		Value value;
		ValueType valueType;
		int minValue;
		int maxValue;
		std::map<int, std::string> lookup;
	};

}
