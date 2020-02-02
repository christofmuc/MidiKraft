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
		SynthHolder(Synth *synth, Colour const &color);
		SynthHolder(SimpleDiscoverableDevice *synth, Colour const &color);
		SynthHolder(SoundExpanderCapability *synth);

		Synth *synth() { return dynamic_cast<Synth *>(device_); }
		SimpleDiscoverableDevice *device() { return dynamic_cast<SimpleDiscoverableDevice *>(device_); }
		SoundExpanderCapability *soundExpander() { return dynamic_cast<SoundExpanderCapability *>(device_); }
		Colour color() { return color_; }
		void setColor(Colour const &newColor);

		static Synth *findSynth(std::vector<SynthHolder> &synths, std::string const &synthName);

	private:
		NamedDeviceCapability *device_;
		Colour color_;
	};

}
