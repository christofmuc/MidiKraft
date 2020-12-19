/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AutomaticCategory.h"

#include "Patch.h"
#include "PatchHolder.h"
#include "StoredTagCapability.h"

#include "BinaryResources.h"
#include "RapidjsonHelper.h"

#include <boost/format.hpp>

namespace midikraft {

	// From http://colorbrewer2.org/#type=qualitative&scheme=Set3&n=12
	std::vector<std::string> colorPalette = { "ff8dd3c7", "ffffffb3", "ff4a75b2", "fffb8072", "ff80b1d3", "fffdb462", "ffb3de69", "fffccde5", "ffd9d9d9", "ffbc80bd", "ffccebc5", "ffffed6f",
		"ff869cab", "ff317469", "ffa75781" };

	AutomaticCategory::AutomaticCategory()
	{
		if (autoCategoryFileExists()) {
			loadFromFile(getAutoCategoryFile().getFullPathName().toStdString());
		}
		else {
			loadFromString(defaultJson());
		}

		if (autoCategoryMappingFileExists()) {
			auto fileContent = getAutoCategoryMappingFile().loadFileAsString();
			loadMappingFromString(fileContent.toStdString());
		}
		else {
			loadMappingFromString(defaultJsonMapping());
		}
	}

	std::vector<AutoCategory> AutomaticCategory::predefinedCategories() {
		return predefinedCategories_;
	}

	std::map<std::string, std::map<std::string, std::string>> const &AutomaticCategory::importMappings()
	{
		return importMappings_;
	}

	std::vector<midikraft::Category> AutomaticCategory::predefinedCategoryVector()
	{
		std::vector<midikraft::Category> result;
		for (auto a : predefinedCategories()) {
			result.push_back(a.category());
		}
		return result;
	}

	std::set<Category> AutomaticCategory::determineAutomaticCategories(PatchHolder const &patch)
	{
		std::set <Category> result;

		// First step, the synth might support stored categories
		auto storedTags = std::dynamic_pointer_cast<StoredTagCapability>(patch.patch());
		if (storedTags) {
			// Ah, that synth supports storing tags in the patch data itself, nice! Let's see if we can use them
			auto tags = storedTags->tags();
			auto mappings = importMappings();
			std::string synthname = patch.synth()->getName();
			for (auto tag : tags) {
				// Let's see if we can map it
				if (mappings.find(synthname) != mappings.end()) {
					if (mappings[synthname].find(tag.name()) != mappings[synthname].end()) {
						std::string categoryName = mappings[synthname][tag.name()];
						if (categoryName != "None") {
							bool found = false;
							for (auto cat : predefinedCategories()) {
								if (cat.category().category == categoryName) {
									// That's us!
									result.insert(cat.category());
									found = true;
								}
							}
							if (!found) {
								SimpleLogger::instance()->postMessage((boost::format("Warning: Invalid mapping for Synth %s and stored category %s. Maps to invalid category %s. Use Categories... Edit mappings... to fix.") % synthname % tag.name() % categoryName).str());
							}
						}
					}
					else {
						SimpleLogger::instance()->postMessage((boost::format("Warning: Synth %s has no mapping defined for stored category %s. Use Categories... Edit mappings... to fix.") % synthname % tag.name()).str());
					}
				}
				else {
					SimpleLogger::instance()->postMessage((boost::format("Warning: Synth %s has no mapping defined for stored categories. Use Categories... Edit mappings... to fix.") % synthname).str());
				}
			}
		}

		if (result.empty()) {
			// Second step, if we have no category yet, try to detect the category from the name using the regex rule set stored in the file automatic_categories.jsonc
			for (auto autoCat : predefinedCategories()) {
				for (auto matcher : autoCat.patchNameMatchers_) {
					bool found = std::regex_search(patch.name(), matcher);
					if (found) {
						result.insert(autoCat.category_);
					}
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

	void AutomaticCategory::loadFromFile(std::string fullPathToJson)
	{
		// Load the string in the file given
		File jsonFile(fullPathToJson);
		if (jsonFile.exists()) {
			auto fileContent = jsonFile.loadFileAsString();
			loadFromString(fileContent.toStdString());
		}
	}

	void AutomaticCategory::loadFromString(std::string const fileContent) {
		// Parse as JSON
		rapidjson::Document doc;
		doc.Parse<rapidjson::kParseCommentsFlag>(fileContent.c_str());
		if (doc.IsObject()) {
			// Replace the hard-coded values with those read from the JSON file
			predefinedCategories_.clear();

			auto obj = doc.GetObject();
			size_t i = 0;
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
				AutoCategory cat(Category(categoryName, colorForIndex(i)), regexes);
				i++;
				predefinedCategories_.push_back(cat);
			}
		}
	}

	void AutomaticCategory::loadMappingFromString(std::string const fileContent) {
		// Parse as JSON
		rapidjson::Document doc;
		doc.Parse<rapidjson::kParseCommentsFlag>(fileContent.c_str());
		if (doc.IsObject()) {
			// Replace the hard-coded values with those read from the JSON file
			importMappings_.clear();

			auto obj = doc.GetObject();
			for (auto member = obj.MemberBegin(); member != obj.MemberEnd(); member++) {
				std::string synth = member->name.GetString();
				if (member->value.HasMember("synthToDatabase")) {
					std::map<std::string, std::string> mapping;
					auto importMap = member->value.FindMember("synthToDatabase");
					if (importMap->value.IsObject()) {
						for (auto s = importMap->value.MemberBegin(); s != importMap->value.MemberEnd(); s++) {
							if (s->name.IsString() && s->value.IsString()) {
								std::string input = s->name.GetString();
								std::string output = s->value.GetString();
								mapping[input] = output;
							}
							else {
								SimpleLogger::instance()->postMessage("Invalid JSON input - need to map strings to strings only");
							}
						}
						importMappings_[synth] =  mapping;
					}
					else {
						SimpleLogger::instance()->postMessage("Invalid JSON input - need to supply map object");
					}
				}
			}
		}
	}

	bool AutomaticCategory::autoCategoryFileExists() const
	{
		File autocat = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("KnobKraft").getChildFile("automatic_categories.jsonc");
		return autocat.exists();
	}

	File AutomaticCategory::getAutoCategoryFile() {
		File appData = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("KnobKraft");
		if (!appData.exists()) {
			appData.createDirectory();
		}
		File jsoncFile = appData.getChildFile("automatic_categories.jsonc");
		if (!jsoncFile.exists()) {
			// Create an initial file from the resources!
			FileOutputStream out(jsoncFile);
			out.writeText(midikraft::AutomaticCategory::defaultJson(), false, false, "\\n");
		}
		return jsoncFile;
	}

	bool AutomaticCategory::autoCategoryMappingFileExists() const
	{
		File automap = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("KnobKraft").getChildFile("mapping_categories.jsonc");
		return automap.exists();
	}

	File AutomaticCategory::getAutoCategoryMappingFile() {
		File appData = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("KnobKraft");
		if (!appData.exists()) {
			appData.createDirectory();
		}
		File jsoncFile = appData.getChildFile("mapping_categories.jsonc");
		if (!jsoncFile.exists()) {
			// Create an initial file from the resources!
			FileOutputStream out(jsoncFile);
			out.writeText(midikraft::AutomaticCategory::defaultJsonMapping(), false, false, "\\n");
		}
		return jsoncFile;
	}

	std::string AutomaticCategory::defaultJson()
	{
		// Read the default Json definition from the binary resources
		return std::string(automatic_categories_jsonc, automatic_categories_jsonc + automatic_categories_jsonc_size);
	}

	std::string AutomaticCategory::defaultJsonMapping()
	{
		// Read the default Json definition from the binary resources
		return std::string(mapping_categories_jsonc, mapping_categories_jsonc + mapping_categories_jsonc_size);
	}

	juce::Colour AutomaticCategory::colorForIndex(size_t i)
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
