/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AutomaticCategory.h"

#include "Capability.h"
#include "Patch.h"
#include "PatchHolder.h"
#include "StoredTagCapability.h"

#include "BinaryResources.h"

#include "fmt/format.h"

namespace midikraft {

	AutomaticCategory::AutomaticCategory(std::vector<Category> existingCats)
	{
		if (autoCategoryFileExists()) {
			SimpleLogger::instance()->postMessageOncePerRun(fmt::format("Overriding built-in automatic category rules with file {}", getAutoCategoryFile().getFullPathName().toStdString()));
			loadFromFile(existingCats, getAutoCategoryFile().getFullPathName().toStdString());
		}
		else {
			loadFromString(existingCats, defaultJson());
		}

		if (autoCategoryMappingFileExists()) {
			SimpleLogger::instance()->postMessageOncePerRun(fmt::format("Overriding built-in import category rules with file {}", getAutoCategoryMappingFile().getFullPathName().toStdString()));
			auto fileContent = getAutoCategoryMappingFile().loadFileAsString();
			try {
				loadMappingFromString(fileContent.toStdString());
			}
			catch (nlohmann::json::exception const& e) {
				SimpleLogger::instance()->postMessage(fmt::format("JSON error loading category import mapping definitions from file {}, file will be ignored: {}", getAutoCategoryMappingFile().getFullPathName().toStdString(), e.what()));
			}
		}
		else {
			loadMappingFromString(defaultJsonMapping());
		}
	}

	std::map<std::string, std::map<std::string, std::string>> const &AutomaticCategory::importMappings()
	{
		return importMappings_;
	}

	std::set<Category> AutomaticCategory::determineAutomaticCategories(PatchHolder const &patch)
	{
		std::set <Category> result;

		// First step, the synth might support stored categories
		auto storedTags = midikraft::Capability::hasCapability<StoredTagCapability>(patch.patch());
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
							auto found = predefinedCategories_.find(categoryName);
							if (found != predefinedCategories_.end()) {
								// That's us!
								result.insert(found->second.category());
							}
							else {
								SimpleLogger::instance()->postMessage(fmt::format("Warning: Invalid mapping for Synth {} and stored category {}. Maps to invalid category {}. Use Categories... Edit mappings... to fix.", synthname, tag.name(),  categoryName));
							}
						}
					}
					else {
						SimpleLogger::instance()->postMessage(fmt::format("Warning: Synth {} has no mapping defined for stored category {}. Use Categories... Edit mappings... to fix.", synthname, tag.name()));
					}
				}
				else {
					SimpleLogger::instance()->postMessage(fmt::format("Warning: Synth {} has no mapping defined for stored categories. Use Categories... Edit mappings... to fix.", synthname));
				}
			}
		}

		if (result.empty()) {
			// Second step, if we have no category yet, try to detect the category from the name using the regex rule set stored in the file automatic_categories.jsonc
			for (auto autoCat : predefinedCategories_) {
				for (auto matcher : autoCat.second.patchNameMatchers_) {
					bool found = std::regex_search(patch.name(), matcher.second);
					if (found) {
						result.insert(autoCat.second.category_);
					}
				}
			}
		}
		return result;
	}

	AutoCategoryRule::AutoCategoryRule(Category category, std::vector<std::string> const &regexes) :
		category_(category)
	{
		for (auto regex : regexes) {
			patchNameMatchers_[regex] = (std::regex(regex, std::regex::icase));
		}
	}

	AutoCategoryRule::AutoCategoryRule(Category category, std::map<std::string, std::regex> const &regexes) :
		category_(category), patchNameMatchers_(regexes)
	{
	}

	Category AutoCategoryRule::category() const
	{
		return category_;
	}

	std::map<std::string, std::regex> AutoCategoryRule::patchNameMatchers() const
	{
		return patchNameMatchers_;
	}

	void AutomaticCategory::loadFromFile(std::vector<Category> existingCats, std::string fullPathToJson)
	{
		// Load the string in the file given
		File jsonFile(fullPathToJson);
		if (jsonFile.exists()) {
			auto fileContent = jsonFile.loadFileAsString();
			try {
				loadFromString(existingCats, fileContent.toStdString());
			}
			catch (nlohmann::json::exception const& e) {
				SimpleLogger::instance()->postMessage(fmt::format("JSON error loading category definitions from file {}, file will be ignored: {}", fullPathToJson, e.what()));
			}
		}
	}

	void AutomaticCategory::loadFromString(std::vector<Category> existingCats, std::string const fileContent) {
		// Parse as JSON, allowing comments in the file
		auto doc = nlohmann::json::parse(fileContent, nullptr, true, true);
		if (doc.is_object()) {
			for (auto const& member : doc.items()) {
				auto categoryName = member.key();
				std::map<std::string, std::regex> regexes;
				if (member.value().is_array()) {
					auto a = member.value();
					for (auto s = a.cbegin(); s != a.cend(); s++) {

						if (s->is_string()) {
							// Simple Regex
							regexes[s->get<std::string>()] = std::regex(s->get<std::string>(), std::regex::icase);
						}
						else if (s->is_object()) {
							bool case_sensitive = false;
							// Regex specifying options
							if (s->contains("case-sensitive")) {
								auto caseness = (*s)["case-sensitive"];
								if (caseness.is_boolean()) {
									case_sensitive = caseness.get<bool>();
								}
							}
							if (s->contains("regex")) {
								auto regex = (*s)["regex"];
								if (regex.is_string()) {
									auto value = regex.get<std::string>();
									regexes[value] = std::regex(value, case_sensitive ? std::regex_constants::ECMAScript : (std::regex::icase));
								}
							}
						}
					}
				}
				// Find it in the existing Categories
				bool found = false;
				for (auto existing : existingCats) {
					if (existing.category() == categoryName) {
						addAutoCategory({ existing, regexes });
						found = true;
						break;
					}
				}
				if (!found) {
					SimpleLogger::instance()->postMessage(fmt::format("Ignoring rules for category {}, because that name is not found in the database", categoryName));
				}
			}
		}
	}

	std::vector<midikraft::AutoCategoryRule> AutomaticCategory::loadedRules() const
	{
		std::vector<midikraft::AutoCategoryRule> result;
		for (auto const& rule : predefinedCategories_) {
			result.push_back(rule.second);
		}
		return result;
	}

	void AutomaticCategory::loadMappingFromString(std::string const fileContent) {
		// Parse as JSON
		auto doc = nlohmann::json::parse(fileContent, nullptr, true, true);
		if (doc.is_object()) {
			// Replace the hard-coded values with those read from the JSON file
			importMappings_.clear();

			for (auto member : doc.items()) {
				std::string synth = member.key();
				if (member.value().contains("synthToDatabase")) {
					std::map<std::string, std::string> mapping;
					auto importMap = member.value()["synthToDatabase"];
					if (importMap.is_object()) {
						for (auto s : importMap.items()) {
							if (s.value().is_string()) {
								std::string input = s.key();
								std::string output = s.value();
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

	void AutomaticCategory::addAutoCategory(AutoCategoryRule const &autoCat)
	{
		auto found = predefinedCategories_.find(autoCat.category_.category());
		if (found == predefinedCategories_.end()) {
			// First time
			predefinedCategories_.emplace(autoCat.category_.category(), autoCat);
		}
		else
		{
			// Already exists, need to update. Take over category definition and merge rules
			found->second.category_ = autoCat.category_;
			found->second.patchNameMatchers_.insert(autoCat.patchNameMatchers_.cbegin(), autoCat.patchNameMatchers_.cend());
		}
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

}
