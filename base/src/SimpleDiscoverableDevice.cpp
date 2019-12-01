/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SimpleDiscoverableDevice.h"

void SimpleDiscoverableDevice::setCurrentChannelZeroBased(std::string const &midiInput, std::string const &midiOutput, int midiChannel)
{
	channel_ = MidiChannel::fromZeroBase(midiChannel & 0x0f); // Allowed values are 0-15
	midiInput_ = midiInput;
	midiOutput_ = midiOutput;
}

void SimpleDiscoverableDevice::setChannel(MidiChannel newChannel)
{
	channel_ = newChannel;
}

std::string SimpleDiscoverableDevice::midiInput() const
{
	return midiInput_;
}

std::string SimpleDiscoverableDevice::midiOutput() const
{
	return midiOutput_;
}

MidiChannel SimpleDiscoverableDevice::channel() const
{
	return channel_;
}
