/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "SimpleDiscoverableDevice.h"
#include "ProgressHandler.h"

namespace midikraft {

	class AutoDetection : public ChangeBroadcaster {
	public:
		void autoconfigure(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths, ProgressHandler *progressHandler);
		void quickconfigure(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths);

	private:
		void findSynth(SimpleDiscoverableDevice *synth, ProgressHandler *progressHandler);
		bool checkSynth(SimpleDiscoverableDevice *synth);
		void listenerToAllFound(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths);
	};

}
