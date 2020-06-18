/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Patch.h"

namespace midikraft {

	class DataFileSendCapability {
	public:
		virtual std::vector<MidiMessage> dataFileToMessages(std::shared_ptr<DataFile> dataFile) const = 0;
	};

}

