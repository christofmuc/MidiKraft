/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SynthHolder.h"

#include "Settings.h"
#include "Synth.h"
#include "SimpleDiscoverableDevice.h"
#include "SoundExpanderCapability.h"

#include <boost/format.hpp>

namespace midikraft {

	std::string colorSynthKey(DiscoverableDevice *synth) {
		return (boost::format("%s-color") % synth->getName()).str();
	}

	SynthHolder::SynthHolder(Synth *synth, Colour const &color) : device_(synth)
	{
		// Override the constructor color with the one from the settings file, if set
		color_ = Colour::fromString(Settings::instance().get(colorSynthKey(synth), color.toString().toStdString()));
	}

	SynthHolder::SynthHolder(SimpleDiscoverableDevice *synth, Colour const &color) : device_(synth)
	{
		// Override the constructor color with the one from the settings file, if set
		color_ = Colour::fromString(Settings::instance().get(colorSynthKey(synth), color.toString().toStdString()));
	}

	SynthHolder::SynthHolder(SoundExpanderCapability *synth) : device_(synth)
	{
	}

	void SynthHolder::setColor(Colour const &newColor)
	{
		color_ = newColor;

		// Additionally, we want to persist this change in the user settings file!
		Settings::instance().set(colorSynthKey(device()), newColor.toString().toStdString());
	}

	Synth * SynthHolder::findSynth(std::vector<SynthHolder> &synths, std::string const &synthName)
	{
		for (auto synth : synths) {
			if (synth.synth() && synth.synth()->getName() == synthName) {
				return synth.synth();
			}
		}
		return nullptr;
	}

}
