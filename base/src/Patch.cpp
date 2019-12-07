/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Patch.h"

#include "SynthParameterDefinition.h"

#include <boost/format.hpp>

namespace midikraft {

	Patch::Patch()
	{
	}

	Patch::Patch(Synth::PatchData const &patchdata) : data_(patchdata)
	{
	}

	void Patch::setData(Synth::PatchData const &data)
	{
		data_ = data;
	}

	Synth::PatchData const & Patch::data() const
	{
		return data_;
	}


	int Patch::at(int sysExIndex) const
	{
		return data_.at(sysExIndex);
	}

	void Patch::setAt(int sysExIndex, uint8 value)
	{
		jassert(sysExIndex < data_.size());
		data_[sysExIndex] = value;
	}

	std::string Patch::patchToTextRaw(bool onlyActive)
	{
		std::string result;

		for (auto param : allParameterDefinitions()) {
			auto activeCheck = dynamic_cast<SynthParameterActiveDetectionCapability *>(param);
			if (!onlyActive || !activeCheck || !(activeCheck->isActive(this))) {
				result = result + (boost::format("%s: %s\n") % param->description() % param->valueInPatchToText(*this)).str();
			}
		}
		return result;
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
