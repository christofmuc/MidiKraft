#pragma once

#include "JuceHeader.h"

#include "NamedDeviceCapability.h"
#include "MidiChannel.h"

class DiscoverableDevice : public virtual NamedDeviceCapability {
public:
	// These need to be implemented by any new device we want to find on the network
	virtual MidiMessage deviceDetect(int channel) = 0;
	virtual int deviceDetectSleepMS() = 0;
	virtual MidiChannel channelIfValidDeviceResponse(const MidiMessage &message) = 0;
	virtual bool needsChannelSpecificDetection() = 0;
};




