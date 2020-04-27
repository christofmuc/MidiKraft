/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Patch.h"

namespace midikraft {

	class DataFileLoadCapability {
	public:
		struct DataFileDescription {
			std::string name; // Display name of the type
			bool canBeRequested; // If you can't request it, it is a write-only type. For example, the Alternate Tunings of the Rev2
			bool canBeSent;  // If you can't send the file type, it is read-only. For example, the global settings of the Rev2
		};

		virtual std::vector<MidiMessage> requestDataItem(int itemNo, int dataTypeID) = 0;
		virtual int numberOfDataItemsPerType(int dataTypeID) const = 0;
		virtual bool isDataFile(const MidiMessage &message, int dataTypeID) const = 0;
		virtual std::vector<std::shared_ptr<DataFile>> loadData(std::vector<MidiMessage> messages, int dataTypeID) const = 0;
		virtual std::vector<DataFileDescription> dataTypeNames() const = 0;
	};

	class DataFileSendCapability {
	public:
		virtual std::vector<MidiMessage> dataFileToMessages(std::shared_ptr<DataFile> dataFile) const = 0;
	};


}

