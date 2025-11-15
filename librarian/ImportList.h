/*
   Copyright (c) 2024 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "PatchList.h"

namespace midikraft {

	class ImportList : public PatchList {
	public:
		ImportList(std::shared_ptr<Synth> synth, std::string const& id, std::string const &name);
        virtual ~ImportList() = default;

		std::shared_ptr<Synth> synth() const;
		
	private:
		std::shared_ptr<Synth> synth_;
	};

}