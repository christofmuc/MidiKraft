/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ImportList.h"

namespace midikraft {

	ImportList::ImportList(std::shared_ptr<Synth> synth, std::string const& id, std::string const& name) : 
		PatchList(id, name)
		, synth_(synth)
	{
	}

	std::shared_ptr<Synth> ImportList::synth() const
	{
		return synth_;
	}

}
