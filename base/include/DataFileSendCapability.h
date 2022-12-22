/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Patch.h"

namespace midikraft {

	class SendTarget {
	public:
		virtual std::string name() const = 0;
	};

	class DataFileSendCapability {
	public:
		virtual std::vector<MidiMessage> dataFileToMessages(std::shared_ptr<DataFile> dataFile, std::shared_ptr<SendTarget> target) const = 0;
	};

	class MultipleSendTargetCapability {
	public:
		virtual std::vector<std::shared_ptr<SendTarget>> getSendTargetNameList() const = 0;
	};


}

