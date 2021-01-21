/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "Category.h"

namespace midikraft {

	class CategoryBitfield {
	public:
		struct BitName {
			std::string name;
			int bitIndex;
			Colour color;
		};

		CategoryBitfield(std::vector<BitName> const &bitNames);

		std::vector<Category> categoryVector() const;
		void makeSetOfCategoriesFromBitfield(std::set<Category> &cats, int64 bitfield) const;
		juce::int64 categorySetAsBitfield(std::set<Category> const &categories) const;

		int maxBitIndex() const;

	private:
		int bitIndexForCategory(Category &category) const;

		std::vector<BitName> bitNames_;
	};

}
