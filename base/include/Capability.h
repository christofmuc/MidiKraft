/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <memory>

namespace midikraft {

	// Base class for classes having a varying set of Capabilities
	template <class D>
	class RuntimeCapability {
	public:
		virtual bool hasCapability(std::shared_ptr<D> &outCapability) const = 0;
		virtual bool hasCapability(D **outCapability) const = 0;
	};

	// Generic accessor for Capabilities - use this instead of std::dynamic_pointer_cast
	class Capability {
	public:
		template <class D, class S>
		static std::shared_ptr<D> hasCapability(std::shared_ptr<S> const &input);

		template <class D, class S>
		static D *hasCapability(S *input);
	};

	template <class D, class S>
	D * midikraft::Capability::hasCapability(S *input)
	{
		auto runtimeCapability = dynamic_cast<RuntimeCapability<D> const *>(input);
		if (runtimeCapability) {
			D *capability;
			if (runtimeCapability->hasCapability(&capability)) {
				return capability;
			}
			else {
				return nullptr;
			}
		}
		else {
			return dynamic_cast<D *>(input);
		}
	}

	template <class D, class S>
	std::shared_ptr<D> midikraft::Capability::hasCapability(std::shared_ptr<S> const &input)
	{
		std::shared_ptr<RuntimeCapability<D>> runtimeCapability = std::dynamic_pointer_cast<RuntimeCapability<D>>(input);
		if (runtimeCapability) {
			std::shared_ptr<D> out;
			runtimeCapability->hasCapability(out);
			return out;
		}
		else {
			return std::dynamic_pointer_cast<D>(input);
		}
	}

}



