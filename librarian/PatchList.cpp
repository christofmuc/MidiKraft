/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchList.h"

namespace midikraft {

	PatchList::PatchList(std::string const &id, std::string const& name) : id_(id), name_(name)
	{
	}

	PatchList::PatchList(std::string const& name) : id_(Uuid().toString().toStdString()), name_(name)
	{
	}

	std::string PatchList::name() const
	{
		return name_;
	}

	void PatchList::setName(std::string const& new_name)
	{
		name_ = new_name;
	}

	void PatchList::setPatches(std::vector<PatchHolder> patches)
	{
		patches_ = patches;
	}

	std::vector<midikraft::PatchHolder> PatchList::patches() const
	{
		return patches_;
	}

	void PatchList::addPatch(PatchHolder patch)
	{
		patches_.push_back(patch);
	}

}
