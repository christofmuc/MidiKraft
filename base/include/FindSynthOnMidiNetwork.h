/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "DiscoverableDevice.h"
#include "MidiNetworkLocation.h"
#include "MidiController.h"
#include "ProgressHandler.h"

namespace midikraft {

	class IsSynth : public MidiInputCallback {
	public:
		IsSynth(DiscoverableDevice &synth);

		virtual void handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message) override;

		std::vector<MidiNetworkLocation> locations() { return found_; }
		void restart() { found_.clear(); }

	private:
		DiscoverableDevice & synth_;
		std::vector<MidiNetworkLocation> found_;
	};


	class FindSynthOnMidiNetwork : public Thread
	{
	public:
		// Implementation of task
		virtual void run() override;

		static std::vector<MidiNetworkLocation> detectSynth(DiscoverableDevice &synth, ProgressHandler *progressHandler);

	private:
		FindSynthOnMidiNetwork(DiscoverableDevice &synth, std::string const &text, ProgressHandler *progressHandler);
		virtual ~FindSynthOnMidiNetwork() override;

		MidiController::HandlerHandle handler_;
		std::weak_ptr<IsSynth> isSynth_; // The synth that is to be detected
		DiscoverableDevice &synth_;
		std::vector<MidiNetworkLocation> locations_;
		ProgressHandler *progressHandler_;
	};

}
