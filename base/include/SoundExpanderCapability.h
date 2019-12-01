#pragma once

#include "MidiChannel.h"
#include "NamedDeviceCapability.h"

#include <functional>

class MidiController;

class SoundExpanderCapability : public virtual NamedDeviceCapability {
public:
	virtual bool canChangeInputChannel() const = 0;
	virtual void changeInputChannel(MidiController *controller, MidiChannel channel, std::function<void()> onFinished) = 0;
	virtual MidiChannel getInputChannel() const = 0;

	virtual bool hasMidiControl() const = 0;
	virtual bool isMidiControlOn() const = 0;
	virtual void setMidiControl(MidiController *controller, bool isOn) = 0;
};
