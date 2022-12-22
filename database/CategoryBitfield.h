/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "Category.h"

namespace midikraft {

	class CategoryBitfield {
	public:
		CategoryBitfield(std::vector<std::shared_ptr<CategoryDefinition>> const &bitNames);

		void makeSetOfCategoriesFromBitfield(std::set<Category> &cats, int64 bitfield) const;
		juce::int64 categorySetAsBitfield(std::set<Category> const &categories) const;

		int maxBitIndex() const;

	private:
		int bitIndexForCategory(Category &category) const;

		std::vector<std::shared_ptr<CategoryDefinition>> bitNames_;
	};

}
