/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "FindSynthOnMidiNetwork.h"

#include "MidiHelpers.h"
#include "MidiCoroutine.h"

#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include "SpdLogJuce.h"

namespace midikraft {

	IsSynth::IsSynth(DiscoverableDevice &synth) : synth_(synth)
	{
	}

	void IsSynth::handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message)
	{
		MidiChannel channel = synth_.channelIfValidDeviceResponse(message);
		if (channel.isValid()) {
			synth_.setWasDetected(true);
			found_.push_back(MidiNetworkLocation(source->getDeviceInfo(), MidiDeviceInfo(), channel));
		}
	}

	FindSynthOnMidiNetwork::FindSynthOnMidiNetwork(DiscoverableDevice &synth, std::string const &text, ProgressHandler *progressHandler) :
		Thread(text), handler_(MidiController::makeOneHandle()), synth_(synth), progressHandler_(progressHandler)
	{
		MidiController::instance()->addMessageHandler(handler_, [this](MidiInput *source, MidiMessage const &midimessage) {
			if (!isSynth_.expired()) {
				isSynth_.lock()->handleIncomingMidiMessage(source, midimessage);
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
		int midiIns = MidiInput::getAvailableDevices().size();
		int midiOuts = MidiOutput::getAvailableDevices().size();

		// This detector can be enabled on all ins during the scan
		std::shared_ptr<IsSynth> callback = std::make_shared<IsSynth>(synth_);
		isSynth_ = callback;

		// Loop over all inputs and enable them, add the callback
		for (int input = 0; input < midiIns; input++) {
			auto inputName = MidiInput::getAvailableDevices()[input];
			MidiController::instance()->enableMidiInput(inputName);
		}

		// Now loop over outputs
		for (int output = 0; output < midiOuts; output++) {
			if (progressHandler_ && progressHandler_->shouldAbort()) break;
			callback->restart();
			if (synth_.needsChannelSpecificDetection()) {
				// Test all 16 channels
				for (int channel = 0; channel < 16; channel++) {
					// Send the synth detection signal
					auto detectMessage = synth_.deviceDetect(channel);
					//TODO:  I cannot use the synth's sendBlockOfMessagesToSynth() here because I do not have a synth pointer. Smell?
					MidiController::instance()->getMidiOutput(MidiOutput::getAvailableDevices()[output])->sendBlockOfMessagesFullSpeed(MidiHelpers::bufferFromMessages(detectMessage));
				}
			}
			else {
				// Just one message is enough - use a "broadcast" channel or sysex device ID as parameter
				auto detectMessage = synth_.deviceDetect(0x7f);
				//TODO:  I cannot use the synth's sendBlockOfMessagesToSynth() here because I do not have a synth pointer. Smell?
				MidiController::instance()->getMidiOutput(MidiOutput::getAvailableDevices()[output])->sendBlockOfMessagesFullSpeed(MidiHelpers::bufferFromMessages(detectMessage));
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
			for (auto const &found : callback->locations()) {
				auto withOutput = found;
				withOutput.output = MidiOutput::getAvailableDevices()[output];
				locations_.push_back(withOutput);
				// Super special case - we might want to terminate the successful device detection with a special message sent to the same output as the detect message!
				MidiMessage endDetectMessage;
				if (synth_.endDeviceDetect(endDetectMessage)) {
					MidiController::instance()->getMidiOutput(MidiOutput::getAvailableDevices()[output])->sendMessageNow(endDetectMessage);
				}
			}
		}

		// Loop over all inputs and turn them off, remove callback
		for (int input = 0; input < midiIns; input++) {
			auto inputName = MidiInput::getAvailableDevices()[input];
			MidiController::instance()->disableMidiInput(inputName);
		}

		
	}

	std::vector<MidiNetworkLocation> FindSynthOnMidiNetwork::detectSynthOld(DiscoverableDevice &synth, ProgressHandler *progressHandler)
	{
		FindSynthOnMidiNetwork m(synth, fmt::format("Looking for {} on your MIDI network...", synth.getName()), progressHandler);
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

	MidiCoroutine<std::vector<MidiNetworkLocation>> asyncDetectSynth(DiscoverableDevice& synth, ProgressHandler* progressHandler) {
		using this_coroutine = MidiCoroutine<std::vector<MidiNetworkLocation>>;
		std::vector<MidiNetworkLocation> locationsFound;

		// Loop over all inputs and enable them so we get messages from them
		for (auto inputDevice : juce::MidiInput::getAvailableDevices()) {
			MidiController::instance()->enableMidiInput(inputDevice);
		}

		std::vector<int> channelsToSearch;
		if (synth.needsChannelSpecificDetection()) {
			int i = 0;
			std::generate_n(std::back_inserter(channelsToSearch), 16,
				[&]() {
					return i++;
				});
		}
		else {
			channelsToSearch.push_back(0x7f);
		}

		// Now loop over all Output Devices
		int outputI = 0;
		int numDevices = juce::MidiOutput::getAvailableDevices().size();
		for (auto outputDevice : juce::MidiOutput::getAvailableDevices()) {
			for (auto channel : channelsToSearch) {
				MidiController::instance()->getMidiOutput(outputDevice)->sendBlockOfMessagesFullSpeed(synth.deviceDetect(channel));
			}
			auto startOfDetection = std::chrono::steady_clock::now();
			do {
				auto singleMessage = co_await this_coroutine::IncomingMidiMessage{};
				spdlog::debug("Got message: {}", singleMessage.message.getDescription());
				auto detectionResult = synth.channelIfValidDeviceResponse(singleMessage.message);
				if (detectionResult.isValid()) {
					spdlog::info("Detected device {} with MIDI Output/Input pair '{}','{}'", synth.getName(), outputDevice.name, singleMessage.device.name);
					synth.setWasDetected(true);
					locationsFound.push_back({ singleMessage.device, outputDevice, detectionResult});

					// Super special case - we might want to terminate the successful device detection with a special message sent to the same output as the detect message!
					MidiMessage endDetectMessage;
					if (synth.endDeviceDetect(endDetectMessage)) {
						MidiController::instance()->getMidiOutput(outputDevice)->sendMessageNow(endDetectMessage);
					}
					break;
				}
			} while (std::chrono::steady_clock::now() - startOfDetection < std::chrono::milliseconds(synth.deviceDetectSleepMS()));
			// this will update the progress bar on the dialog box
			if (progressHandler) progressHandler->setProgressPercentage(++outputI / (double)numDevices);
		}
		co_return locationsFound;
	}

	std::vector<MidiNetworkLocation> FindSynthOnMidiNetwork::detectSynth(DiscoverableDevice & synth, ProgressHandler * progressHandler) {
		ignoreUnused(progressHandler);
		//MidiControllerInput<MidiCoroutine<std::vector<MidiNetworkLocation>>::promise_type> input;
		auto coroutine = asyncDetectSynth(synth, progressHandler);
		coroutine.handle.resume();
		return coroutine.getResult();
	}




}