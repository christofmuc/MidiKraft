/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "TimedMidiSender.h"

#include "MidiController.h"
#include "Logger.h"

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

	void TimedMidiSender::addMessageToBuffer(juce::MidiDeviceInfo const &midiOutput, MidiMessage &message, double timeRelativeToNowInS)
	{
		auto timestamp = Time::getMillisecondCounterHiRes() * 0.001 + timeRelativeToNowInS - startTime_;
		message.setTimeStamp(timestamp);
		auto sampleNumber = (int)(timestamp * sampleRate_);
		if (midiBuffer_.find(midiOutput.identifier) == midiBuffer_.end()) {
			midiBuffer_[midiOutput.identifier] = MidiBuffer();
		}
		jassert(message.getRawDataSize() <= 65535);
		midiDevices_[midiOutput.identifier] = midiOutput; // Save for later
		midiBuffer_[midiOutput.identifier].addEvent(message, sampleNumber);
	}

	void TimedMidiSender::timerCallback()
	{
		auto currentTime = Time::getMillisecondCounterHiRes() * 0.001 - startTime_;
		auto currentSampleNumber = (int)(currentTime * sampleRate_);

		for (auto &buffer : midiBuffer_) {
			MidiBuffer outputEvents;
			for (auto m : buffer.second)
			{
				if (m.samplePosition > currentSampleNumber)
					break;

				MidiMessage message = m.getMessage();
				message.setTimeStamp(m.samplePosition / sampleRate_);
				outputEvents.addEvent(message, 0);
			}

			if (!outputEvents.isEmpty()) {
				if (midiDevices_.find(buffer.first) != midiDevices_.end()) {
					midikraft::MidiController::instance()->getMidiOutput(midiDevices_[buffer.first])->sendBlockOfMessagesFullSpeed(outputEvents);
				}
				else {
					SimpleLogger::instance()->postMessageOncePerRun("Can't send to unknown MIDI output - program error?");
				}
			}
			buffer.second.clear(previousSampleNumber_, currentSampleNumber - previousSampleNumber_); 
		}

		previousSampleNumber_ = currentSampleNumber;
	}

}