/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <memory>

namespace midikraft {

	class Capability {
	public:
		template <class D, class S>
		static std::shared_ptr<D> hasCapability(std::shared_ptr<S> const &input);
	};

	template <class D, class S>
	static std::shared_ptr<D>
		midikraft::Capability::hasCapability(std::shared_ptr<S> const &input)
	{
		return std::dynamic_pointer_cast<D>(input);
	}

}



