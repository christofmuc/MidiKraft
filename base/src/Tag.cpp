/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Tag.h"

namespace midikraft {

	Tag::Tag(std::string const &name) : name_(name)
	{
	}

	void Tag::setName(std::string const &name)
	{
		name_ = name;
	}

	std::string Tag::name() const
	{
		return name_;
	}

	bool Tag::operator<(const Tag &other) const
	{
		return name_ < other.name_;
	}

}

