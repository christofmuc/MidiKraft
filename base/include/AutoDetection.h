/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "SimpleDiscoverableDevice.h"
#include "ProgressHandler.h"

#include "MidiController.h"
#include "FindSynthOnMidiNetwork.h"

namespace midikraft {

	class AutoDetection : public ChangeBroadcaster {
	public:
		AutoDetection() = default;
		virtual ~AutoDetection() = default;

		void autoconfigure(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths, ProgressHandler *progressHandler);
		void quickconfigure(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths, ProgressHandler *progressHandler = nullptr);
		static bool hasSavedConnection(SimpleDiscoverableDevice *synth);
		static void persistSetting(SimpleDiscoverableDevice *synth);
		static void loadSettings(SimpleDiscoverableDevice *synth);

	private:
		void findSynth(SimpleDiscoverableDevice *synth, ProgressHandler *progressHandler);
		bool checkSynth(SimpleDiscoverableDevice *synth, ProgressHandler *progressHandler);
		void listenerToAllFound(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths);

	};

}
