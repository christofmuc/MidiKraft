/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Category.h"

#include <algorithm>

namespace midikraft {

	bool operator==(Category const &left, Category const &right)
	{
		// Ignore bitIndex and color
		return left.def_->id == right.def_->id;
	}

	bool operator<(Category const &left, Category const &right)
	{
		return left.def_->id < right.def_->id;
	}

	std::set<midikraft::Category> category_union(std::set<Category> const &a, std::set<Category> const &b)
	{
		std::set<Category> result;
		std::set_union(a.cbegin(), a.cend(), b.cbegin(), b.cend(), std::inserter(result, result.begin()));
		return result;
	}

	std::set<midikraft::Category> category_intersection(std::set<Category> const &a, std::set<Category> const &b)
	{
		std::set<Category> result;
		std::set_intersection(a.cbegin(), a.cend(), b.cbegin(), b.cend(), std::inserter(result, result.begin()));
		return result;
	}

	std::set<midikraft::Category> category_difference(std::set<Category> const &a, std::set<Category> const &b)
	{
		std::set<Category> result;
		std::set_difference(a.cbegin(), a.cend(), b.cbegin(), b.cend(), std::inserter(result, result.begin()));
		return result;
	}

	std::string Category::category() const
	{
		return def_->name;
	}

	juce::Colour Category::color() const
	{
		return def_->color;
	}

	std::shared_ptr<midikraft::CategoryDefinition> Category::def() const
	{
		return def_;
	}

}