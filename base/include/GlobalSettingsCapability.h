/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "TypedNamedValue.h"
#include "Patch.h"
#include "DataFileLoadCapability.h"

namespace midikraft {

	class GlobalSettingsCapability {
	public:
		virtual void setGlobalSettingsFromDataFile(std::shared_ptr<DataFile> dataFile) = 0;
		virtual std::vector<std::shared_ptr<TypedNamedValue>> getGlobalSettings() = 0;
		virtual DataFileLoadCapability *loader() = 0;
		virtual int settingsDataFileType() const = 0;
	};

}
