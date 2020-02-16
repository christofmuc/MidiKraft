/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AutomaticCategory.h"

#include "Patch.h"

namespace midikraft {

	std::vector<AutoCategory> AutoCategory::predefinedCategories_;

	// From http://colorbrewer2.org/#type=qualitative&scheme=Set3&n=12
	std::vector<std::string> colorPalette = { "ff8dd3c7", "ffffffb3", "ffbebada", "fffb8072", "ff80b1d3", "fffdb462", "ffb3de69", "fffccde5", "ffd9d9d9", "ffbc80bd", "ffccebc5", "ffffed6f" };

	std::vector<AutoCategory> AutoCategory::predefinedCategories() {
		// Lazy init
		if (predefinedCategories_.empty()) {
			predefinedCategories_ = {
			{ { "Lead", Colour::fromString(colorPalette[0]), 1 },{ "^ld", "ld$", "lead", "uni", "solo" } },
			{ { "Pad", Colour::fromString(colorPalette[1]), 2 },{ "pad", "pd ", "pd$", "^pd", "str ", "str$", "strg", "strng", "string", "bow" } },
			{ { "Brass", Colour::fromString(colorPalette[2]), 3 },{ "horn", "hrn", "brass", "brs$", "trumpet" } },
			{ { "Organ", Colour::fromString(colorPalette[3]), 4 },{ "b3", "hammond", "org", "farf", "church", "pipe" } },
			{ { "Keys", Colour::fromString(colorPalette[4]), 5 },{ "wurl", "pian", "rhode", "pno", "clav", "klav", " ep", "key" } },
			{ { "Bass", Colour::fromString(colorPalette[5]), 6 },{ "^bs[^a-z]", "bs$", "bass", "bas$", " ba$" } },
			{ { "Arp", Colour::fromString(colorPalette[6]), 7 },{ "[^h]arp" } },
			{ { "Pluck", Colour::fromString(colorPalette[7]), 8 },{ "pluck", "gitar", "guitar", "harp" } },
			{ { "Drone", Colour::fromString(colorPalette[8]), 9 },{ "drone" } },
			{ { "Drum", Colour::fromString(colorPalette[9]), 10 },{ "snare", "base", "bd$", "dr\\.", "drum", " tom", "kick", "perc" } },
			{ { "Bells", Colour::fromString(colorPalette[10]), 11 },{ "bell", "tines", "chime" } },
			{ { "FX", Colour::fromString(colorPalette[11]), 12 },{ "fx" } },
			};
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

	Category AutoCategory::category() const
	{
		return category_;
	}

	bool operator<(Category const &left, Category const &right)
	{
		return left.category < right.category;
	}

}
