/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

namespace midikraft {

	template <typename T>
	struct PluginTraits {
		static const char *filename;
	};

	template <typename T>
	class DynamicPlugin : public DynamicLibrary {
		typedef  T *(*createFunctionType)();
		typedef  void(*deleteFunctionType)(T *);

		template <typename T>
		T getTypedFunction(const String& functionName) {
			return (T)this->getFunction(functionName);
		}

	public:
		DynamicPlugin() : DynamicLibrary(PluginTraits<T>::filename) {
		}

		~DynamicPlugin() {
			close();
		}

		T *create() {
			auto createFn = getTypedFunction<createFunctionType>("CreateObjectInstance");
			if (createFn) {
				return (*createFn)();
			}
			else {
				jassert(false);
				return nullptr;
			}
		}

		void destroy(T *object) {
			auto deleteFn = getTypedFunction<deleteFunctionType>("ReleaseObject");
			if (deleteFn) {
				(*deleteFn)(object);
			}
			else {
				jassert(false);
			}
		}
	};
}
