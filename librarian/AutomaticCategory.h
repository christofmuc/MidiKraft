/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Category.h"

#include <set>
#include <map>
#include <regex>

namespace midikraft {

	class PatchHolder;

	class AutoCategoryRule {
	public:
		AutoCategoryRule(Category category, std::vector<std::string> const &regexes);
		AutoCategoryRule(Category category, std::vector<std::regex> const &regexes);
		Category category() const;

		std::vector<std::regex> patchNameMatchers() const;

	private:
		friend class AutomaticCategory; // Refactoring help

		Category category_;
		std::vector<std::regex> patchNameMatchers_;
	};

	class AutomaticCategory {
	public:
		AutomaticCategory();

		std::vector<AutoCategoryRule> predefinedCategories();
		std::vector<Category> predefinedCategoryVector();
		std::set<Category> determineAutomaticCategories(PatchHolder const &patch);
		std::map<std::string, std::map<std::string, std::string>> const &importMappings();

		void loadFromFile(std::string fullPathToJson);
		void loadFromString(std::string const fileContent);

		bool autoCategoryFileExists() const;
		bool autoCategoryMappingFileExists() const;

		File getAutoCategoryFile();
		File getAutoCategoryMappingFile();

		void addAutoCategory(AutoCategoryRule const &autoCat);

		static Colour colorForIndex(size_t i);

	private:
		void loadMappingFromString(std::string const fileContent);

		std::string defaultJson();
		std::string defaultJsonMapping();

		std::vector<AutoCategoryRule> predefinedCategories_;
		std::map<std::string, std::map<std::string, std::string>> importMappings_;
	};

}
