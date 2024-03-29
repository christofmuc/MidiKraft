/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MidiRequest.h"

#include "WaitForEvent.h"
#include "MidiController.h"

namespace midikraft {

	midikraft::MidiRequest::MidiRequest(juce::MidiDeviceInfo const &midiOutput, std::vector<MidiMessage> const &request, TIsAnswerPredicate pred) : output_(midiOutput), request_(request), pred_(pred)
	{
	}

	void MidiRequest::blockUntilTrue(std::function<bool()> pred, int timeOutInMilliseconds /* = 2000 */) {
		// Busy wait thread
		WaitForEvent waiting(pred);
		waiting.startThread();
		if (waiting.waitForThreadToExit(timeOutInMilliseconds)) {
			std::cout << "MidiRequest succeeded" << std::endl;
		}
		else {
			std::cerr << "Timeout while waiting for MidiRequest result" << std::endl;
			throw std::runtime_error("PyTschirp: Timeout while waiting for midi request");
		}
	}

	juce::MidiMessage midikraft::MidiRequest::blockForReply()
	{
		auto handler = midikraft::MidiController::makeOneHandle();
		bool answered = false;
		MidiMessage answer;
		midikraft::MidiController::instance()->addMessageHandler(handler, [this, &answered, &answer](MidiInput *source, MidiMessage const &message) {
			ignoreUnused(source);
			if (pred_(message)) {
				answer = message;
				answered = true;
			}
		});
		midikraft::MidiController::instance()->getMidiOutput(output_)->sendBlockOfMessagesFullSpeed(request_);
		try {
			blockUntilTrue([&answered]() { return answered; });
			midikraft::MidiController::instance()->removeMessageHandler(handler);
			return answer;
		}
		catch (std::runtime_error &e) {
			ignoreUnused(e);
			midikraft::MidiController::instance()->removeMessageHandler(handler);
			throw std::runtime_error("PyTschirp: Timeout while waiting for edit buffer midi message");
		}
	}

}
