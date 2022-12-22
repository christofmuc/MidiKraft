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
		AutoCategoryRule(Category category, std::map<std::string, std::regex> const &regexes);
		Category category() const;

		std::map<std::string, std::regex> patchNameMatchers() const;

	private:
		friend class AutomaticCategory; // Refactoring help

		Category category_;
		std::map<std::string, std::regex> patchNameMatchers_;
	};

	class AutomaticCategory {
	public:
		AutomaticCategory(std::vector<Category> existingCats);

		std::set<Category> determineAutomaticCategories(PatchHolder const &patch);
		std::map<std::string, std::map<std::string, std::string>> const &importMappings();

		void loadFromFile(std::vector<Category> existingCats, std::string fullPathToJson);
		void loadFromString(std::vector<Category> existingCats, std::string const fileContent);
		std::vector<AutoCategoryRule> loadedRules() const;

		bool autoCategoryFileExists() const;
		bool autoCategoryMappingFileExists() const;

		File getAutoCategoryFile();
		File getAutoCategoryMappingFile();

		void addAutoCategory(AutoCategoryRule const &autoCat);

	private:
		void loadMappingFromString(std::string const fileContent);

		std::string defaultJson();
		std::string defaultJsonMapping();

		std::map<std::string, AutoCategoryRule> predefinedCategories_;
		std::map<std::string, std::map<std::string, std::string>> importMappings_;
	};

}
