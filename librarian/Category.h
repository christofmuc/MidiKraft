/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include <set>

namespace midikraft {

	struct CategoryDefinition {
		int id;
		bool isActive;
		std::string name;
		Colour color;
	};

	class Category {
	public:
		Category(std::shared_ptr<CategoryDefinition> def) : def_(def) {}
		std::string category() const;
		Colour color() const;
		std::shared_ptr<CategoryDefinition> def() const;

	private:
		friend bool operator ==(Category const &left, Category const &right);
		friend bool operator <(Category const &left, Category const &right);

		std::shared_ptr<CategoryDefinition> def_;
	};

	std::set<Category> category_union(std::set<Category> const &a, std::set<Category> const &b);
	std::set<Category> category_intersection(std::set<Category> const &, std::set<Category> const &);
	std::set<Category> category_difference(std::set<Category> const &, std::set<Category> const &);

	bool operator <(Category const &left, Category const &right);
	bool operator ==(Category const &left, Category const &right);
	

}
