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

		MidiRequest(std::string const &midiOutput, MidiMessage const &request, TIsAnswerPredicate pred);
		MidiMessage blockForReply();

	private:
		std::string output_;
		MidiMessage request_;
		TIsAnswerPredicate pred_;
	};

}