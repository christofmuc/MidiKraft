/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "DiscoverableDevice.h"

#include "MidiLocationCapability.h"

namespace midikraft {

	class SimpleDiscoverableDevice : public DiscoverableDevice, public MidiLocationCapability {
	public:
		virtual void setCurrentChannelZeroBased(std::string const &midiInput, std::string const &midiOutput, int midiChannel);
		virtual void setChannel(MidiChannel newChannel);
		virtual void setInput(std::string const &midiInput); // To override auto-detection
		virtual void setOutput(std::string const &midiOutput); // To override auto-detection
		
		// DiscoverableDevice
		virtual bool wasDetected() const override;
		virtual void setWasDetected(bool wasDetected) override;

		// MidiLocationCapability
		virtual std::string midiInput() const override;
		virtual std::string midiOutput() const override;
		virtual MidiChannel channel() const override;

	private:
		MidiChannel channel_ = MidiChannel::invalidChannel();
		bool wasDetected_ = false;
		std::string midiInput_;
		std::string midiOutput_;
	};

}