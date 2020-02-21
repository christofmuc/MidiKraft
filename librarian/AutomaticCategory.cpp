/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AutomaticCategory.h"

#include "Patch.h"

#include "BinaryResources.h"
#include "RapidjsonHelper.h"

namespace midikraft {

	std::vector<AutoCategory> AutoCategory::predefinedCategories_;

	// From http://colorbrewer2.org/#type=qualitative&scheme=Set3&n=12
	std::vector<std::string> colorPalette = { "ff8dd3c7", "ffffffb3", "ffbebada", "fffb8072", "ff80b1d3", "fffdb462", "ffb3de69", "fffccde5", "ffd9d9d9", "ffbc80bd", "ffccebc5", "ffffed6f" };

	std::vector<AutoCategory> AutoCategory::predefinedCategories() {
		// Lazy init
		if (predefinedCategories_.empty()) {
			loadFromString(defaultJson());
		}
		return predefinedCategories_;
	}

	std::vector<midikraft::Category> AutoCategory::predefinedCategoryVector()
	{
		std::vector<midikraft::Category> result;
		for (auto a : predefinedCategories()) {
			result.push_back(a.category());
		}
		return result;
	}

	std::set<Category> AutoCategory::determineAutomaticCategories(Patch const &patch)
	{
		std::set <Category> result;
		for (auto autoCat : predefinedCategories()) {
			for (auto matcher : autoCat.patchNameMatchers_) {
				bool found = std::regex_search(patch.patchName(), matcher);
				if (found) {
					result.insert(autoCat.category_);
				}
			}
		}
		return result;
	}

	AutoCategory::AutoCategory(Category category, std::vector<std::string> const &regexes) :
		category_(category)
	{
		for (auto regex : regexes) {
			patchNameMatchers_.push_back(std::regex(regex, std::regex::icase));
		}
	}

	AutoCategory::AutoCategory(Category category, std::vector<std::regex> const &regexes) :
		category_(category), patchNameMatchers_(regexes)
	{
	}

	Category AutoCategory::category() const
	{
		return category_;
	}

	void AutoCategory::loadFromFile(std::string fullPathToJson)
	{
		// Load the string in the file given
		File jsonFile(fullPathToJson);
		if (jsonFile.exists()) {
			auto fileContent = jsonFile.loadFileAsString();
			loadFromString(fileContent.toStdString());
		}
	}

	void AutoCategory::loadFromString(std::string const fileContent) {
		// Parse as JSON
		rapidjson::Document doc;
		doc.Parse<rapidjson::kParseCommentsFlag>(fileContent.c_str());
		if (doc.IsObject()) {
			// Replace the hard-coded values with those read from the JSON file
			predefinedCategories_.clear();

			auto obj = doc.GetObject();
			size_t i = 1;
			for (auto member = obj.MemberBegin(); member != obj.MemberEnd(); member++) {
				auto categoryName = member->name.GetString();
				std::vector<std::regex> regexes;
				if (member->value.IsArray()) {
					auto a = member->value.GetArray();
					for (auto s = a.Begin(); s != a.End(); s++) {

						if (s->IsString()) {
							// Simple Regex
							regexes.push_back(std::regex(s->GetString(), std::regex::icase));
						}
						else if (s->IsObject()) {
							bool case_sensitive = false;
							// Regex specifying options
							if (s->HasMember("case-sensitive")) {
								auto caseness = s->FindMember("case-sensitive");
								if (caseness->value.IsBool()) {
									case_sensitive = caseness->value.GetBool();
								}
							}
							if (s->HasMember("regex")) {
								auto regex = s->FindMember("regex");
								if (regex->value.IsString()) {
									regexes.push_back(std::regex(s->GetString(), case_sensitive ? std::regex_constants::ECMAScript : (std::regex::icase)));
								}
							}
						}
					}
				}
				AutoCategory cat(Category(categoryName, colorForIndex(i - 1), (int) i), regexes);
				i++;
				predefinedCategories_.push_back(cat);
			}
		}
	}

	std::string AutoCategory::defaultJson()
	{
		// Read the default Json definition from the binary resources
		return std::string(automatic_categories_jsonc, automatic_categories_jsonc + automatic_categories_jsonc_size);
	}

	juce::Colour AutoCategory::colorForIndex(size_t i)
	{
		if (i < colorPalette.size()) {
			return Colour::fromString(colorPalette[i]);
		}
		else {
			return Colours::darkgrey;
		}
	}

	bool operator<(Category const &left, Category const &right)
	{
		return left.category < right.category;
	}

}
