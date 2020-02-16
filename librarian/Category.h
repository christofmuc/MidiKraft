/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

namespace midikraft {

	class Category {
	public:
		Category(std::string const &c, Colour o, int i) : category(c), color(o), bitIndex(i) {}
		std::string category;
		Colour color;
		int bitIndex; // For bit-vector storage 
	};

	bool operator <(Category const &left, Category const &right);

}
