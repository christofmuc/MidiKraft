/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <string>

namespace midikraft {

	class StoredPatchNameCapability {
	public:
		virtual void setName(std::string const &name) = 0;
		virtual bool isDefaultName() const = 0;
	};

}
