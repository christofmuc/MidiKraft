/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Patch.h"

#include "SynthParameterDefinition.h"
#include "LayeredPatch.h"

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

	Synth::PatchData const & DataFile::data() const
	{
		return data_;
	}


	int DataFile::at(int sysExIndex) const
	{
		return data_.at(sysExIndex);
	}

	void DataFile::setAt(int sysExIndex, uint8 value)
	{
		jassert(((size_t) sysExIndex) < data_.size());
		data_[sysExIndex] = value;
	}

	std::string Patch::patchToTextRaw(bool onlyActive)
	{
		std::string result;

		int numLayers = 1;
		auto layers = dynamic_cast<LayeredPatch *>(this);
		if (layers) {
			numLayers = layers->numberOfLayers();
		}

		for (int layer = 0; layer < numLayers; layer++) {
			if (layers) {
				if (layer > 0) result += "\n";
				result = result + (boost::format("Layer: %s\n") % layers->layerName(layer)).str();
			}
			for (auto param : allParameterDefinitions()) {
				if (layers) {
					auto multiLayerParam = std::dynamic_pointer_cast<SynthMultiLayerParameterCapability>(param);
					jassert(multiLayerParam);
					if (multiLayerParam) {
						multiLayerParam->setTargetLayer(layer);
					}
				}
				auto activeCheck = std::dynamic_pointer_cast<SynthParameterActiveDetectionCapability>(param);
				if (!onlyActive || !activeCheck || !(activeCheck->isActive(this))) {
					result = result + (boost::format("%s: %s\n") % param->description() % param->valueInPatchToText(*this)).str();
				}
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
