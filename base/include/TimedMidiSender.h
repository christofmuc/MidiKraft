/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

namespace midikraft {

	class TimedMidiSender : private Timer {
	public:
		TimedMidiSender(int sampleRate);
		virtual ~TimedMidiSender() override;

		void addMessageToBuffer(std::string const &midiOutput, MidiMessage &message, double timeRelativeToNowInS);

		void timerCallback() override;

	private:
		int sampleRate_;
		int previousSampleNumber_;
		double startTime_;
		std::map<std::string, MidiBuffer> midiBuffer_;
	};

}
