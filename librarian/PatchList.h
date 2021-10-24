/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "PatchHolder.h"

namespace midikraft {

	class PatchList {
	public:
		PatchList(std::string const& name);
		PatchList(std::string const& id, std::string const &name);
		
		std::string id() const;
		std::string name() const;
		void setName(std::string const& new_name);

		void setPatches(std::vector<PatchHolder> patches);
		std::vector<PatchHolder> patches() const;
		void addPatch(PatchHolder patch);
		

	private:
		std::string id_;
		std::string name_;
		std::vector<PatchHolder> patches_;
	};

}