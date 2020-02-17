/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Category.h"

juce::int64 midikraft::Category::categorySetAsBitfield(std::set<Category> const &categories)
{
	uint64 mask = 0;
	for (auto cat : categories) {
		mask |= 1LL << (cat.bitIndex - 1); // bitIndex is 1-based
		jassert(cat.bitIndex > 0 && cat.bitIndex < 64);
	}
	return mask;
}

bool midikraft::operator==(Category const &left, Category const &right)
{
	// Ignore bitIndex and color
	return left.category == right.category;
}
