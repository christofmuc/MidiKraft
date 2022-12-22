/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <string>

namespace midikraft {

	class Tag {
	public:
		Tag(std::string const &name);

		virtual void setName(std::string const &name);
		virtual std::string name() const;

		bool operator <(const Tag &other) const;

	private:
		std::string name_;
	};


}
