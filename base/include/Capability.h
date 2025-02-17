/*
   Copyright (c) 2020-2025 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <memory>

namespace midikraft {

	// Registry for capabilities - stores capabilities per instance (like Rev2)
	class CapabilityRegistry {
	public:
		// Register a capability for a non-const instance
		template <typename CapabilityType>
		void registerCapability(void* synthInstance, CapabilityType *capabilityInstance) {
			capabilities_[synthInstance][typeid(CapabilityType)] = capabilityInstance;
		}

		// Register a capability for a const instance
		template <typename CapabilityType>
		void registerCapability(const void* synthInstance) {
			capabilities_[const_cast<void*>(synthInstance)][typeid(CapabilityType)] = std::make_shared<CapabilityType>();
		}

		// Get a capability for a non-const instance
		template <typename CapabilityType>
		CapabilityType * getCapability(void* synthInstance) {
			auto& instanceCapabilities = capabilities_[synthInstance];
			auto it = instanceCapabilities.find(typeid(CapabilityType));
			if (it != instanceCapabilities.end()) {
				return static_cast<CapabilityType *>(it->second);
			}
			return nullptr;
		}

		// Get a capability for a const instance
		template <typename CapabilityType>
		const CapabilityType* getCapability(const void* synthInstance) const {
			auto& instanceCapabilities = capabilities_.at(const_cast<void*>(synthInstance));
			auto it = instanceCapabilities.find(typeid(CapabilityType));
			if (it != instanceCapabilities.end()) {
				return static_cast<const CapabilityType *>(it->second);
			}
			return nullptr;
		}

	private:
		// A map of instance type -> capability type -> capability instance
		std::unordered_map<void*, std::unordered_map<std::type_index, void *>> capabilities_;
	};

	extern CapabilityRegistry globalCapabilityRegistry;


}



