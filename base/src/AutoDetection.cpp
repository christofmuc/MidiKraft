/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AutoDetection.h"

#include "Logger.h"
#include "Settings.h"
#include "MidiHelpers.h"

#include <boost/format.hpp>

namespace midikraft {

	const char
		*kChannel = "channel",
		*kInput = "input",
		*kOutput = "output";

	std::string midiSetupKey(DiscoverableDevice *synth, std::string const &trait) {
		return (boost::format("%s-%s") % synth->getName() % trait).str();
	}

	AutoDetection::AutoDetection() : handler_(MidiController::makeOneHandle())
	{
		MidiController::instance()->addMessageHandler(handler_, [this](MidiInput *source, MidiMessage const &midimessage) {
			if (!isSynth_.expired()) {
				isSynth_.lock()->handleIncomingMidiMessage(source, midimessage);
			}
		});
	}

	AutoDetection::~AutoDetection()
	{
		MidiController::instance()->removeMessageHandler(handler_);
	}

	void AutoDetection::autoconfigure(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths, ProgressHandler *progressHandler)
	{
		// For all devices that are discoverable, run the find method
		for (auto synthHolder : allSynths) {
			if (synthHolder) {
				findSynth(synthHolder.get(), progressHandler);
			}
		}
		listenerToAllFound(allSynths);
		sendChangeMessage();
	}

	void AutoDetection::quickconfigure(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths)
	{
		// For all devices that are discoverable, run the find method
		for (auto &synthHolder : allSynths) {
			if (synthHolder) {
				auto synth = synthHolder.get();
				// Load the synthesizer setup from the settings file
				loadSettings(synth);
				// Hack - if the wait time is negative, don't autodetect. This needs to be replaced by some proper dynamic cast
				if (synthHolder->deviceDetectSleepMS() < 0) {
					continue;
				}
				if (!checkSynth(synth)) {
					SimpleLogger::instance()->postMessage(
						(boost::format("Lost communication with %s on channel %d of device %s - please rerun auto-detect synths!")
							% synth->getName() % synth->channel().toOneBasedInt() % synth->midiOutput()).str());
				}
				else {
					SimpleLogger::instance()->postMessage((boost::format("Detected %s on channel %d of device %s") 
						% synth->getName() % synth->channel().toOneBasedInt() % synth->midiOutput()).str());
				}
		}
		}
		listenerToAllFound(allSynths);
		sendChangeMessage();
	}

	void AutoDetection::persistSetting(SimpleDiscoverableDevice *synth)
	{
		if (synth->channel().isValid()) {
			Settings::instance().set(midiSetupKey(synth, kChannel), (boost::format("%d") % synth->channel().toZeroBasedInt()).str());
		}
		Settings::instance().set(midiSetupKey(synth, kInput), synth->midiInput());
		Settings::instance().set(midiSetupKey(synth, kOutput), synth->midiOutput());
	}

	void AutoDetection::loadSettings(SimpleDiscoverableDevice *synth)
	{
		std::string input = Settings::instance().get(midiSetupKey(synth, kInput));
		synth->setInput(input);
		std::string output = Settings::instance().get(midiSetupKey(synth, kOutput));
		synth->setOutput(output);

		synth->setChannel(MidiChannel::invalidChannel());
		std::string channelString = Settings::instance().get(midiSetupKey(synth, kChannel));
		if (!channelString.empty()) {
			int channel = std::atoi(channelString.c_str());
			if (channel >= 0 && channel < 16) {
				synth->setChannel(MidiChannel::fromZeroBase(channel));
			}
		}
	}

	void AutoDetection::findSynth(SimpleDiscoverableDevice *synth, ProgressHandler *progressHandler) {
		// Hack - if the wait time is negative, don't autodetect. This needs to be replaced by some proper dynamic cast
		if (synth->deviceDetectSleepMS() < 0) {
			return;
		}

		if (progressHandler) {
			progressHandler->setMessage((boost::format("Trying to detect %s...") % synth->getName()).str());
		}

		auto locations = FindSynthOnMidiNetwork::detectSynth(*synth, progressHandler);
		if (locations.size() > 0) {
			for (auto loc : locations) {
				SimpleLogger::instance()->postMessage((boost::format("Found %s on channel %d replying on device %s when sending to %s on channel %d")
					% synth->getName() % (loc.midiChannel.toOneBasedInt()) % loc.inputName % loc.outputName % loc.midiChannel.toOneBasedInt()).str());
			}

			// Select the last location (the first one might be the "All" devices which we don't want to address the devices individually)
			size_t loc = locations.size() - 1;
			synth->setCurrentChannelZeroBased(locations[loc].inputName, locations[loc].outputName, locations[loc].midiChannel.toZeroBasedInt());

			// Additionally, we want to persist this knowhow in the user settings file!
			persistSetting(synth);
		}
		else {
			// Ups
			SimpleLogger::instance()->postMessage((boost::format("No %s could be detected - is it turned on?") % synth->getName()).str());
		}
	}

	bool AutoDetection::checkSynth(SimpleDiscoverableDevice *synth) {
		// Hack - if the wait time is negative, don't autodetect. This needs to be replaced by some proper dynamic cast
		if (synth->deviceDetectSleepMS() < 0) {
			return false;
		}

		// This is the fast version of the FindSynthOnMidiNetwork routine - just a single pass to see if the synth responds
		std::shared_ptr<IsSynth> callback = std::make_shared<IsSynth>(*synth);
		isSynth_ = callback;
		MidiController::instance()->enableMidiInput(synth->midiInput());

		// Send the detect message
		auto detectMessage = synth->deviceDetect(synth->channel().toZeroBasedInt() & 0x7f);
		MidiController::instance()->getMidiOutput(synth->midiOutput())->sendBlockOfMessagesNow(MidiHelpers::bufferFromMessages(detectMessage));

		// Sleep as long as the synth thinks is enough
		Thread::sleep(synth->deviceDetectSleepMS());

		// Check if we found it
		bool ok = false;
		for (auto found : callback->locations()) {
			if (found.inputName == synth->midiInput() && found.midiChannel.toZeroBasedInt() == synth->channel().toZeroBasedInt()) {
				ok = true;
				// Super special case - we might want to terminate the successful device detection with a special message sent to the same output as the detect message!
				MidiMessage endDetectMessage;
				if (synth->endDeviceDetect(endDetectMessage)) {
					MidiController::instance()->getMidiOutput(synth->midiOutput())->sendMessageNow(endDetectMessage);
				}
			}
		}
		MidiController::instance()->disableMidiInput(synth->midiInput());
		synth->setWasDetected(ok);
		return ok;
	}

	void AutoDetection::listenerToAllFound(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths) {
		// Listen to all detected synths
		for (auto synth : allSynths) {
			if (synth->wasDetected()) {
				MidiController::instance()->enableMidiInput(synth->midiInput());
			}
		}
	}

}
