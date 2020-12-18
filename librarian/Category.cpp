/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Category.h"

bool midikraft::operator==(Category const &left, Category const &right)
{
	// Ignore bitIndex and color
	return left.category == right.category;
}
