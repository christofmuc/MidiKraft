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

		std::shared_ptr<Synth> synth() { return std::dynamic_pointer_cast<Synth>(device_); }
		std::shared_ptr<SimpleDiscoverableDevice> device() { return std::dynamic_pointer_cast<SimpleDiscoverableDevice>(device_); }
		std::shared_ptr<SoundExpanderCapability> soundExpander() { return std::dynamic_pointer_cast<SoundExpanderCapability>(device_); }
		Colour color() { return color_; }
		void setColor(Colour const &newColor);

		std::string getName() const;

		static std::shared_ptr<Synth> findSynth(std::vector<SynthHolder> &synths, std::string const &synthName);

	private:
		std::shared_ptr<NamedDeviceCapability> device_;
		Colour color_;
	};

}
