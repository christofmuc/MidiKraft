/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "FindSynthOnMidiNetwork.h"

#include <boost/format.hpp>

namespace midikraft {

	IsSynth::IsSynth(DiscoverableDevice &synth) : synth_(synth)
	{
	}

	void IsSynth::handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message)
	{
		MidiChannel channel = synth_.channelIfValidDeviceResponse(message);
		if (channel.isValid()) {
			found_.push_back(MidiNetworkLocation(source->getName().toStdString(), "unknown", channel));
		}
	}

	FindSynthOnMidiNetwork::FindSynthOnMidiNetwork(MidiController *midiController, DiscoverableDevice &synth, std::string const &text, ProgressHandler *progressHandler) :
		midiController_(midiController), synth_(synth), Thread(text), progressHandler_(progressHandler)
	{
	}

	void FindSynthOnMidiNetwork::run()
	{
		// We will do the following - select a MIDI in, and send the "Device ID" message to all MIDI outs.
		// If none found, repeat with the next MIDI in
		int midiIns = MidiInput::getDevices().size();
		int midiOuts = MidiOutput::getDevices().size();

		// This detector can be enabled on all ins during the scan
		IsSynth callback(synth_);

		// Loop over all inputs and enable them, add the callback
		for (int input = 0; input < midiIns; input++) {
			auto inputName = MidiInput::getDevices()[input];
			midiController_->enableMidiInput(inputName.toStdString());
		}
		MidiController::HandlerHandle handler = MidiController::makeOneHandle();
		midiController_->addMessageHandler(handler, [&](MidiInput *source, MidiMessage const &midimessage) {
			callback.handleIncomingMidiMessage(source, midimessage);
		});

		// Now loop over outputs
		for (int output = 0; output < midiOuts; output++) {
			if (progressHandler_ && progressHandler_->shouldAbort()) break;
			callback.reset();
			if (synth_.needsChannelSpecificDetection()) {
				// Test all 16 channels
				for (int channel = 0; channel < 16; channel++) {
					// Send the synth detection signal
					auto detectMessage = synth_.deviceDetect(channel);
					midiController_->getMidiOutput(MidiOutput::getDevices()[output].toStdString())->sendMessageNow(detectMessage);
				}
			}
			else {
				// Just one message is enough
				auto detectMessage = synth_.deviceDetect(-1);
				midiController_->getMidiOutput(MidiOutput::getDevices()[output].toStdString())->sendMessageNow(detectMessage);
			}

			// Sleep
			Thread::sleep(synth_.deviceDetectSleepMS());

			// must check this as often as possible, because this is
			// how we know if the user's pressed 'cancel'
			if (threadShouldExit())
				break;

			// this will update the progress bar on the dialog box
			if (progressHandler_) progressHandler_->setProgressPercentage(output / (double)midiOuts);

			// Copy results
			for (auto found : callback.locations()) {
				auto withOutput = found;
				withOutput.outputName = MidiOutput::getDevices()[output].toStdString();
				locations_.push_back(withOutput);
				// Super special case - we might want to terminate the successful device detection with a special message sent to the same output as the detect message!
				MidiMessage endDetectMessage;
				if (synth_.endDeviceDetect(endDetectMessage)) {
					midiController_->getMidiOutput(MidiOutput::getDevices()[output].toStdString())->sendMessageNow(endDetectMessage);
				}
			}
		}

		// Loop over all inputs and turn them off, remove callback
		for (int input = 0; input < midiIns; input++) {
			auto inputName = MidiInput::getDevices()[input];
			midiController_->disableMidiInput(inputName.toStdString());
		}
		midiController_->removeMessageHandler(handler);
	}

	std::vector<MidiNetworkLocation> FindSynthOnMidiNetwork::detectSynth(MidiController *midiController, DiscoverableDevice &synth, ProgressHandler *progressHandler)
	{
		FindSynthOnMidiNetwork m(midiController, synth, (boost::format("Looking for %s on your MIDI network...") % synth.getName()).str(), progressHandler);
		m.startThread();
		if (m.waitForThreadToExit(15000))
		{
			// thread finished normally..
			return m.locations_;
		}
		else
		{
			// user pressed the cancel button respectively timeout, return empty list
			return std::vector<MidiNetworkLocation>();
		}
	}

}