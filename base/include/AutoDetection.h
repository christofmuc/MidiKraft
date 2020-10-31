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
		AutoDetection();
		virtual ~AutoDetection();

		void autoconfigure(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths, ProgressHandler *progressHandler);
		void quickconfigure(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths);
		static void persistSetting(SimpleDiscoverableDevice *synth);
		static void loadSettings(SimpleDiscoverableDevice *synth);

	private:
		void findSynth(SimpleDiscoverableDevice *synth, ProgressHandler *progressHandler);
		bool checkSynth(SimpleDiscoverableDevice *synth);
		void listenerToAllFound(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths);

		MidiController::HandlerHandle handler_;
		std::weak_ptr<IsSynth> isSynth_; // The synth currently detected by the single callback function. Can be expired in case we gave up on this
	};

}
