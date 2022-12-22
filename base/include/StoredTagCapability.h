/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "Tag.h"

#include <set>

namespace midikraft {

	class StoredTagCapability {
	public:
		virtual bool setTags(std::set<Tag> const &tags) = 0;
		virtual std::set<Tag> tags() const = 0;
	};

}

