#pragma once

#include "MidiChannel.h"

class MidiController;

class MasterkeyboardCapability {
public:
	virtual void changeOutputChannel(MidiController *controller, MidiChannel newChannel, std::function<void()> onFinished) = 0;
	virtual MidiChannel getOutputChannel() const = 0;

	// This is only relevant if the master keyboard itself has sound generation capabilities
	virtual bool hasLocalControl() const = 0;
	virtual void setLocalControl(MidiController *controller, bool localControlOn) = 0;
	virtual bool getLocalControl() const = 0;
};
