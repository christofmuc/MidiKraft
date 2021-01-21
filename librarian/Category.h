/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include <set>

namespace midikraft {

	class Category {
	public:
		Category(std::string const &c, Colour o) : category(c), color(o) {}
		std::string category;
		Colour color;		
	};

	std::set<Category> category_union(std::set<Category> const &a, std::set<Category> const &b);
	std::set<Category> category_intersection(std::set<Category> const &, std::set<Category> const &);
	std::set<Category> category_difference(std::set<Category> const &, std::set<Category> const &);

	bool operator <(Category const &left, Category const &right);
	bool operator ==(Category const &left, Category const &right);
	

}
