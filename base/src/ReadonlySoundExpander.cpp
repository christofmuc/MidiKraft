/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ReadonlySoundExpander.h"

#include <stdexcept>

namespace midikraft {

	bool ReadonlySoundExpander::canChangeInputChannel() const
	{
		return false;
	}

	void ReadonlySoundExpander::changeInputChannel(MidiController *controller, MidiChannel channel, std::function<void()> onFinished)
	{
		ignoreUnused(controller, channel, onFinished);
		throw std::runtime_error("Illegal state");
	}

	bool ReadonlySoundExpander::hasMidiControl() const
	{
		return false;
	}

	bool ReadonlySoundExpander::isMidiControlOn() const
	{
		return true;
	}

	void ReadonlySoundExpander::setMidiControl(MidiController *controller, bool isOn)
	{
		ignoreUnused(controller, isOn);
		throw std::runtime_error("Illegal state");
	}

}
