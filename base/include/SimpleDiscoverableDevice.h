#pragma once

#include "DiscoverableDevice.h"
#include "MidiChannel.h"

class SimpleDiscoverableDevice : public DiscoverableDevice {
public:
	virtual void setCurrentChannelZeroBased(std::string const &midiInput, std::string const &midiOutput, int midiChannel);
	virtual void setChannel(MidiChannel newChannel);
	virtual std::string midiInput() const;
	virtual std::string midiOutput() const;
	virtual MidiChannel channel() const;

private:
	MidiChannel channel_ = MidiChannel::invalidChannel();
	std::string midiInput_;
	std::string midiOutput_;
};
