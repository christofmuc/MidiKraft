/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "CategoryBitfield.h"

#include "AutomaticCategory.h"

namespace midikraft {

	//std::vector<std::string> kLegacyBitIndexNames = { "Lead", "Pad", "Brass", "Organ", "Keys", "Bass", "Arp", "Pluck", "Drone", "Drum", "Bell", "SFX", "Ambient", "Wind",  "Voice" };

	CategoryBitfield::CategoryBitfield(std::vector<BitName> const &bitNames) : bitNames_(bitNames)
	{
	}

	std::vector<midikraft::Category> CategoryBitfield::categoryVector() const
	{
		std::vector<midikraft::Category> result;
		for (auto c : bitNames_) {
			result.push_back({ c.name, c.color});
		}
		return result;
	}

	void midikraft::CategoryBitfield::makeSetOfCategoriesFromBitfield(std::set<Category> &cats, int64 bitfield) const
	{
		cats.clear();
		for (int i = 0; i < 63; i++) {
			if (bitfield & (1LL << i)) {
				// This bit is set, find the category that has this bitindex
				if (i < bitNames_.size()) {
					cats.insert(Category(bitNames_[i].name, bitNames_[i].color));
				}
				else {
					jassertfalse;
				}
			}
		}
	}

	juce::int64 CategoryBitfield::categorySetAsBitfield(std::set<Category> const &categories) const
	{
		uint64 mask = 0;
		for (auto cat : categories) {
			int bitindex = bitIndexForCategory(cat);
			if (bitindex != -1) {
				mask |= 1LL << bitindex;
				jassert(bitindex >= 0 && bitindex < 63);
			}
			else {
				jassertfalse;
			}
		}
		return mask;
	}

	int CategoryBitfield::maxBitIndex() const
	{
		auto max = std::max_element(bitNames_.begin(), bitNames_.end(), [](BitName a, BitName b) { return a.bitIndex < b.bitIndex; });
		if (max != bitNames_.end()) {
			return max->bitIndex;
		} 
		return 0;
	}

	int CategoryBitfield::bitIndexForCategory(Category &category) const {
		for (int i = 0; i < bitNames_.size(); i++) {
			if (category.category == bitNames_[i].name) {
				return bitNames_[i].bitIndex;
			}
		}
		return -1;
	}



}
