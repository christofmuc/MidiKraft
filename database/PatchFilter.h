/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "Synth.h"
#include "Category.h"

namespace midikraft {

	enum class PatchOrdering {
		No_ordering,
		Order_by_Name, // then bank, then program
		Order_by_Import_id, // then bank, then program. This is the default, but not really useful?
		Order_by_Place_in_List
	};

	class PatchFilter {
	public:
		std::map<std::string, std::weak_ptr<Synth>> synths;
		PatchOrdering orderBy;
		std::string importID;
		std::string listID;
		std::string name;
		bool onlyFaves;
		bool onlySpecifcType;
		int typeID;
		bool showHidden;
		bool onlyUntagged;
		std::set<Category> categories; 
		bool andCategories; // Turns OR into AND
		bool onlyDuplicateNames;
	};

	// Inequality operator for patch filters - this can be used to e.g. match if a database query result is for a specific filter setup
	bool operator !=(PatchFilter const& a, PatchFilter const& b);

}