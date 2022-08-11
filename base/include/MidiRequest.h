/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

namespace midikraft {


	class MidiRequest {
	public:
		typedef std::function<bool(MidiMessage const &)> TIsAnswerPredicate;

		MidiRequest(juce::MidiDeviceInfo const& midiOutput, std::vector<MidiMessage> const& request, TIsAnswerPredicate pred);
		MidiMessage blockForReply();

		static void blockUntilTrue(std::function<bool()> pred, int timeOutInMilliseconds = 2000);

	private:
		juce::MidiDeviceInfo output_;
		std::vector<MidiMessage> request_;
		TIsAnswerPredicate pred_;
	};

}