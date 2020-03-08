#pragma once

#include "JuceHeader.h"

namespace midikraft {


	class DataFileLoadCapability {
	public:
		virtual std::vector<MidiMessage> requestDataItem(int itemNo, int dataTypeID) = 0;
		virtual int numberOfDataItemsPerType(int dataTypeID) = 0;
		virtual bool isDataFile(const MidiMessage &message, int dataTypeID) = 0;
		virtual void loadData(std::vector<MidiMessage> messages, int dataTypeID) = 0;
		virtual std::vector<std::string> dataTypeNames() = 0;
	};


}

