/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Category.h"

#include <algorithm>

bool midikraft::operator==(Category const &left, Category const &right)
{
	// Ignore bitIndex and color
	return left.category == right.category;
}

std::set<midikraft::Category> midikraft::category_union(std::set<Category> const &a, std::set<Category> const &b)
{
	std::set<Category> result;
	std::set_union(a.cbegin(), a.cend(), b.cbegin(), b.cend(), std::inserter(result, result.begin()));
	return result;
}

std::set<midikraft::Category> midikraft::category_intersection(std::set<Category> const &a, std::set<Category> const &b)
{
	std::set<Category> result;
	std::set_intersection(a.cbegin(), a.cend(), b.cbegin(), b.cend(), std::inserter(result, result.begin()));
	return result;
}

std::set<midikraft::Category> midikraft::category_difference(std::set<Category> const &a, std::set<Category> const &b)
{
	std::set<Category> result;
	std::set_difference(a.cbegin(), a.cend(), b.cbegin(), b.cend(), std::inserter(result, result.begin()));
	return result;
}
