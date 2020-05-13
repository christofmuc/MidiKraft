/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

namespace midikraft {

	class DataFile;

	class LayerCapability {
	public:
		virtual void switchToLayer(int layerNo) = 0;
		virtual MidiBuffer layerToSysex(std::shared_ptr<DataFile> const patch, int sourceLayer, int targetLayer) const = 0;
	};

}
