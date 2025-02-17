/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Synth.h"
#include "SoundExpanderCapability.h"

namespace midikraft {

	class SynthHolder {
	public:
		SynthHolder(std::shared_ptr<SimpleDiscoverableDevice> synth, Colour const &color);
		SynthHolder(std::shared_ptr<SoundExpanderCapability> synth);
		virtual ~SynthHolder() = default;

		std::shared_ptr<Synth> synth() { auto device_synth = dynamic_pointer_cast<Synth>(device_); return device_synth ? device_synth : dynamic_pointer_cast<Synth>(expander_); }
		std::shared_ptr<SimpleDiscoverableDevice> device() { return device_; }
		std::shared_ptr<SoundExpanderCapability> soundExpander() { auto device_expander = dynamic_pointer_cast<SoundExpanderCapability>(device_); return device_expander ? device_expander : expander_;  }
		Colour color() { return color_; }
		void setColor(Colour const &newColor);

		std::string getName() const;

		static std::shared_ptr<Synth> findSynth(std::vector<SynthHolder> &synths, std::string const &synthName);

	private:
		std::shared_ptr<SimpleDiscoverableDevice> device_;
		std::shared_ptr<SoundExpanderCapability> expander_;
		Colour color_;
	};

}
