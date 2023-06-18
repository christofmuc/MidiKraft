/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "NamedDeviceCapability.h"
#include "MidiChannel.h"

namespace midikraft {

	class DiscoverableDevice {
	public:
		// These need to be implemented by any new device we want to find on the network
		virtual std::vector<juce::MidiMessage> deviceDetect(int channel) = 0;
		virtual int deviceDetectSleepMS() = 0;
		virtual MidiChannel channelIfValidDeviceResponse(const MidiMessage &message) = 0;
		virtual bool needsChannelSpecificDetection() = 0;
		virtual bool endDeviceDetect(MidiMessage &endMessageOut) const;
		virtual bool wasDetected() const = 0;
		virtual void setWasDetected(bool wasDetected) = 0;
	};

}



