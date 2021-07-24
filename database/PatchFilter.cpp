/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchFilter.h"


namespace midikraft {

	bool operator!=(PatchFilter const& a, PatchFilter const& b)
	{
		// Check complex fields 
		for (auto const& asynth : a.synths) {
			if (b.synths.find(asynth.first) == b.synths.end()) {
				return true;
			}
		}
		for (auto const& bsynth : b.synths) {
			if (a.synths.find(bsynth.first) == a.synths.end()) {
				return true;
			}
		}

		if (a.categories != b.categories)
			return true;

		// Then check simple fields
		return a.importID != b.importID
			|| a.name != b.name
			|| a.listID != b.listID
			|| a.onlyFaves != b.onlyFaves
			|| a.onlySpecifcType != b.onlySpecifcType
			|| a.typeID != b.typeID
			|| a.showHidden != b.showHidden
			|| a.onlyUntagged != b.onlyUntagged;
	}


}