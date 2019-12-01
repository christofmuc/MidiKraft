/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "MidiChannel.h"

#include <functional>

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
