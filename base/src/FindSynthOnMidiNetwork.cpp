/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "FindSynthOnMidiNetwork.h"

#include "MidiHelpers.h"

#include "fmt/format.h"

namespace midikraft {

	IsSynth::IsSynth(DiscoverableDevice &synth) : synth_(synth)
	{
	}

	void IsSynth::handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message)
	{
		if (!source || MidiController::isTimeoutMessage(message)) return;
		MidiChannel channel = synth_.channelIfValidDeviceResponse(message);
		if (channel.isValid()) {
			ScopedLock lock(lock_);
			found_.push_back(MidiNetworkLocation(source->getDeviceInfo(), MidiDeviceInfo(), channel));
		}
	}

	FindSynthOnMidiNetwork::FindSynthOnMidiNetwork(DiscoverableDevice &synth, std::string const &text, ProgressHandler *progressHandler) :
		Thread(text), handler_(MidiController::makeOneHandle()), isSynth_(std::make_shared<IsSynth>(synth)), synth_(synth), progressHandler_(progressHandler)
	{
		MidiController::instance()->addMessageHandler(handler_, [weak = std::weak_ptr<IsSynth>(isSynth_)](MidiInput *source, MidiMessage const &midimessage) {
			if (auto callback = weak.lock()) {
				callback->handleIncomingMidiMessage(source, midimessage);
			}
		});
	}

	FindSynthOnMidiNetwork::~FindSynthOnMidiNetwork()
	{
		MidiController::instance()->removeMessageHandler(handler_);
	}

	void FindSynthOnMidiNetwork::run()
	{
		// We will do the following - select a MIDI in, and send the "Device ID" message to all MIDI outs.
		// If none found, repeat with the next MIDI in
		if (progressHandler_ && progressHandler_->shouldAbort()) return;
		// Keep one snapshot: hot-plugging must not change the meaning of an output index.
		auto inputs = MidiInput::getAvailableDevices();
		auto outputs = MidiOutput::getAvailableDevices();
		int midiOuts = outputs.size();

		// This detector can be enabled on all ins during the scan
		auto callback = isSynth_;

		// Loop over all inputs and enable them, add the callback
		std::vector<std::unique_ptr<ScopedMidiInput>> inputListeners;
		for (auto const &input : inputs) {
			inputListeners.push_back(std::make_unique<ScopedMidiInput>(*MidiController::instance(), input));
		}

		// Now loop over outputs
		for (int output = 0; output < midiOuts; output++) {
			if (progressHandler_ && progressHandler_->shouldAbort()) break;
			auto midiOutput = MidiController::instance()->getMidiOutput(outputs[output]);
			if (!midiOutput->isValid()) continue;
			callback->restart();
			if (synth_.needsChannelSpecificDetection()) {
				// Test all 16 channels
				for (int channel = 0; channel < 16; channel++) {
					if (progressHandler_ && progressHandler_->shouldAbort()) break;
					// Send the synth detection signal
					auto detectMessage = synth_.deviceDetect(channel);
					//TODO:  I cannot use the synth's sendBlockOfMessagesToSynth() here because I do not have a synth pointer. Smell?
					midiOutput->sendBlockOfMessagesFullSpeed(MidiHelpers::bufferFromMessages(detectMessage));
				}
			}
			else {
				// Just one message is enough - use a "broadcast" channel or sysex device ID as parameter
				auto detectMessage = synth_.deviceDetect(0x7f);
				//TODO:  I cannot use the synth's sendBlockOfMessagesToSynth() here because I do not have a synth pointer. Smell?
				midiOutput->sendBlockOfMessagesFullSpeed(MidiHelpers::bufferFromMessages(detectMessage));
			}

			// Sleep
			for (int remaining = synth_.deviceDetectSleepMS(); remaining > 0; remaining -= 20) {
				if (progressHandler_ && progressHandler_->shouldAbort()) break;
				Thread::sleep(std::min(remaining, 20));
			}

			// must check this as often as possible, because this is
			// how we know if the user's pressed 'cancel'
			if (progressHandler_ && progressHandler_->shouldAbort())
				break;

			// this will update the progress bar on the dialog box
			if (progressHandler_) progressHandler_->setProgressPercentage((output + 1) / (double)midiOuts);

			// Copy results
			for (auto const &found : callback->locations()) {
				auto withOutput = found;
				withOutput.output = outputs[output];
				locations_.push_back(withOutput);
				// Super special case - we might want to terminate the successful device detection with a special message sent to the same output as the detect message!
				MidiMessage endDetectMessage;
				if (synth_.endDeviceDetect(endDetectMessage)) {
					midiOutput->sendMessageNow(endDetectMessage);
				}
			}
		}

		// Scoped listeners restore the inputs on completion, cancellation and exceptions.
		
	}

	std::vector<MidiNetworkLocation> FindSynthOnMidiNetwork::detectSynth(DiscoverableDevice &synth, ProgressHandler *progressHandler)
	{
		auto nameCap = dynamic_cast<NamedDeviceCapability*>(&synth);
		FindSynthOnMidiNetwork m(synth, fmt::format("Looking for {} on your MIDI network...", nameCap ? nameCap->getName() : "'invalid name'"), progressHandler);
		// The caller already runs detection on a worker. Do not destroy a still-running
		// nested thread after an arbitrary 15-second timeout on larger MIDI networks.
		m.run();
		return m.locations_;
	}

}
