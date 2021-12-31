/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SimpleDiscoverableDevice.h"

namespace midikraft {

	void SimpleDiscoverableDevice::setCurrentChannelZeroBased(juce::MidiDeviceInfo const &midiInput, juce::MidiDeviceInfo const &midiOutput, int midiChannel)
	{
		channel_ = MidiChannel::fromZeroBase(midiChannel & 0x0f); // Allowed values are 0-15
		midiInput_ = midiInput;
		midiOutput_ = midiOutput;
	}

	void SimpleDiscoverableDevice::setChannel(MidiChannel newChannel)
	{
		channel_ = newChannel;
	}

	void SimpleDiscoverableDevice::setInput(juce::MidiDeviceInfo const &midiInput)
	{
		midiInput_ = midiInput;
	}

	void SimpleDiscoverableDevice::setOutput(juce::MidiDeviceInfo const &midiOutput)
	{
		midiOutput_ = midiOutput;
	}

	bool SimpleDiscoverableDevice::wasDetected() const
	{
		return wasDetected_;
	}

	void SimpleDiscoverableDevice::setWasDetected(bool wasDetected)
	{
		wasDetected_ = wasDetected;
	}

	juce::MidiDeviceInfo SimpleDiscoverableDevice::midiInput() const
	{
		return midiInput_;
	}

	juce::MidiDeviceInfo SimpleDiscoverableDevice::midiOutput() const
	{
		return midiOutput_;
	}

	MidiChannel SimpleDiscoverableDevice::channel() const
	{
		return channel_;
	}

}
