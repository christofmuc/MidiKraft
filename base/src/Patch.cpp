/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Patch.h"

#include "SynthParameterDefinition.h"

#include <boost/format.hpp>

namespace midikraft {

	DataFile::DataFile(int dataTypeID) : dataTypeID_(dataTypeID)
	{
	}

	DataFile::DataFile(int dataTypeID, Synth::PatchData const &patchdata) : dataTypeID_(dataTypeID), data_(patchdata)
	{
	}

	int DataFile::dataTypeID() const
	{
		return dataTypeID_;
	}

	void DataFile::setData(Synth::PatchData const &data)
	{
		data_ = data;
	}

	void DataFile::setDataFromSysex(MidiMessage const &message)
	{
		data_ = std::vector<uint8>(message.getSysExData(), message.getSysExData() + message.getSysExDataSize());
	}

	Synth::PatchData const & DataFile::data() const
	{
		return data_;
	}


	int DataFile::at(int sysExIndex) const
	{
		return data_.at((size_t) sysExIndex);
	}

	void DataFile::setAt(int sysExIndex, uint8 value)
	{
		jassert(((size_t) sysExIndex) < data_.size());
		data_[sysExIndex] = value;
	}

	Synth::PatchData Patch::blankOut(std::vector<Range<int>> const &blankoutZones, Synth::PatchData const &inputData)
	{
		auto dataCopy = inputData;
		for (auto blankOut : blankoutZones) {
			for (int i = blankOut.getStart(); i < blankOut.getEnd(); i++) {
				dataCopy[i] = 0;
			}
		}
		return dataCopy;
	}

	}
