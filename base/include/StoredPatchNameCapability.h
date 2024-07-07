/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <string>

namespace midikraft {

	class StoredPatchNameCapability {
	public:
		virtual std::string name() const = 0;
		virtual bool changeNameStoredInPatch(std::string const& name) = 0;
	};

	class DefaultNameCapability {
	public:
		virtual bool isDefaultName(std::string const &patchName) const = 0;
	};

}
