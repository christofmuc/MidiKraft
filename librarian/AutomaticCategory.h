/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include <set>
#include <regex>

namespace midikraft {

	class Patch;

	struct Category {
		Category(std::string const &cat, Colour const &col) : category(cat), color(col) {
		}
		std::string category;
		Colour color;
	};

	bool operator <(Category const &left, Category const &right);

	class AutoCategory {
	public:
		static std::vector<AutoCategory> predefinedCategories();
		static std::set<Category> determineAutomaticCategories(Patch const &patch);

		AutoCategory(Category category, std::vector<std::string> const &regexes);
		Category category() const;

	private:
		static std::vector<AutoCategory> predefinedCategories_;
		Category category_;
		std::vector<std::regex> patchNameMatchers_;
	};

}
