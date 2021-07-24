/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "Synth.h"
#include "Category.h"

namespace midikraft {

	class PatchFilter {
	public:
		std::map<std::string, std::weak_ptr<Synth>> synths;
		std::string importID;
		std::string listID;
		std::string name;
		bool onlyFaves;
		bool onlySpecifcType;
		int typeID;
		bool showHidden;
		bool onlyUntagged;
		std::set<Category> categories;
	};

	// Inequality operator for patch filters - this can be used to e.g. match if a database query result is for a specific filter setup
	bool operator !=(PatchFilter const& a, PatchFilter const& b);

}