/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "SimpleDiscoverableDevice.h"
#include "TypedNamedValue.h"
#include "DataFileLoadCapability.h"

namespace midikraft {

	class StepSequencerStep {
	public:
		virtual bool isOn() = 0;
	};

	class StepSequencerPattern {
	public:
		virtual int numberOfTracks() const = 0;
		virtual std::vector<std::string> trackNames() const = 0;
		virtual std::vector<std::shared_ptr<StepSequencerStep>> track(int trackNo) = 0;
	};

	class StepSequenerDataIdentifier {
	public:
		virtual int getID() = 0;
	};

	class StepSequencer : public SimpleDiscoverableDevice, public DataFileLoadCapability {
	public:
		virtual int numberOfSongs() const = 0;
		virtual int numberOfPatternsPerSong() const = 0;

		virtual std::shared_ptr<StepSequencerPattern> activePattern() = 0;
		virtual std::vector<std::shared_ptr<TypedNamedValue>> properties() = 0;
	};
}
