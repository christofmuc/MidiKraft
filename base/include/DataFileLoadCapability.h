#pragma once

#include "JuceHeader.h"

namespace midikraft {


	class DataFileLoadCapability {
	public:
		struct DataFileDescription {
			std::string name; // Display name of the type
			bool canBeRequested; // If you can't request it, it is a write-only type. For example, the Alternate Tunings of the Rev2
			bool canBeSent;  // If you can't send the file type, it is read-only. For example, the global settings of the Rev2
		};

		virtual std::vector<MidiMessage> requestDataItem(int itemNo, int dataTypeID) = 0;
		virtual int numberOfDataItemsPerType(int dataTypeID) = 0;
		virtual bool isDataFile(const MidiMessage &message, int dataTypeID) = 0;
		virtual void loadData(std::vector<MidiMessage> messages, int dataTypeID) = 0;
		virtual std::vector<DataFileDescription> dataTypeNames() = 0;
	};


}

