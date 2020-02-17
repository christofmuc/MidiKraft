/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Category.h"

#include <set>
#include <regex>

namespace midikraft {

	class Patch;

	class AutoCategory {
	public:
		static std::vector<AutoCategory> predefinedCategories();
		static std::vector<Category> predefinedCategoryVector();
		static std::set<Category> determineAutomaticCategories(Patch const &patch);

		AutoCategory(Category category, std::vector<std::string> const &regexes);		
		AutoCategory(Category category, std::vector<std::regex> const &regexes);
		Category category() const;

		static void loadFromFile(std::string fullPathToJson);
		static void loadFromString(std::string const fileContent);
		static std::string defaultJson(); 

	private:
		static Colour colorForIndex(size_t i);

		static std::vector<AutoCategory> predefinedCategories_;

		Category category_;
		std::vector<std::regex> patchNameMatchers_;
	};

}
