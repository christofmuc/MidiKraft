/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "TimedMidiSender.h"

#include "MidiController.h"

namespace midikraft {

	TimedMidiSender::TimedMidiSender(int sampleRate) : sampleRate_(sampleRate), previousSampleNumber_(0)
	{
		startTime_ = (Time::getMillisecondCounterHiRes() * 0.001);
		// This is jittery like hell but for now we will be ok
		startTimer(50);
	}

	TimedMidiSender::~TimedMidiSender()
	{
		stopTimer();
	}

	void TimedMidiSender::addMessageToBuffer(std::string const &midiOutput, MidiMessage &message, double timeRelativeToNowInS)
	{
		auto timestamp = Time::getMillisecondCounterHiRes() * 0.001 + timeRelativeToNowInS - startTime_;
		message.setTimeStamp(timestamp);
		auto sampleNumber = (int)(timestamp * sampleRate_);
		if (midiBuffer_.find(midiOutput) == midiBuffer_.end()) {
			midiBuffer_[midiOutput] = MidiBuffer();
		}
		midiBuffer_[midiOutput].addEvent(message, sampleNumber);
	}

	void TimedMidiSender::timerCallback()
	{
		auto currentTime = Time::getMillisecondCounterHiRes() * 0.001 - startTime_;
		auto currentSampleNumber = (int)(currentTime * sampleRate_);

		for (auto &buffer : midiBuffer_) {
			MidiBuffer outputEvents;
			MidiBuffer::Iterator iterator(buffer.second);
			MidiMessage message;
			int sampleNumber;

			while (iterator.getNextEvent(message, sampleNumber))
			{
				if (sampleNumber > currentSampleNumber)
					break;

				message.setTimeStamp(sampleNumber / sampleRate_);
				outputEvents.addEvent(message, 0);
			}

			if (!outputEvents.isEmpty()) {
				midikraft::MidiController::instance()->getMidiOutput(buffer.first)->sendBlockOfMessagesNow(outputEvents);
			}
			buffer.second.clear(previousSampleNumber_, currentSampleNumber - previousSampleNumber_); 
		}

		previousSampleNumber_ = currentSampleNumber;
	}

}