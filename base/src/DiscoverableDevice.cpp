/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "DiscoverableDevice.h"

namespace midikraft {

	bool DiscoverableDevice::endDeviceDetect(MidiMessage &endMessageOut) const
	{
		ignoreUnused(endMessageOut);
		// Default is that we do not need the endDeviceDetect message
		return false;
	}

}
