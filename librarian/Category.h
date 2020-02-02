/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

namespace midikraft {

	struct Category {
		Category(std::string const &cat, Colour const &col) : category(cat), color(col) {
		}
		std::string category;
		Colour color;
	};

	bool operator <(Category const &left, Category const &right);

}
