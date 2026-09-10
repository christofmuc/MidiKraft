/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AutoDetection.h"

#include "Logger.h"
#include "Settings.h"
#include "MidiHelpers.h"

#include <spdlog/spdlog.h>

namespace midikraft {

	const char
		*kChannel = "channel",
		*kInput = "input",
		*kOutput = "output";

	static std::string midiSetupKey(DiscoverableDevice *synth, std::string const &trait) {
		auto nameCap = dynamic_cast<NamedDeviceCapability*>(synth);
		return fmt::format("{}-{}", nameCap ? nameCap->getName() : "invalid", trait);
	}

	static int savedChannel(SimpleDiscoverableDevice *synth) {
		auto text = Settings::instance().get(midiSetupKey(synth, kChannel));
		if (text.empty() || text.size() > 2 || text.find_first_not_of("0123456789") != std::string::npos) return -1;
		int channel = std::stoi(text);
		return channel < 16 ? channel : -1;
	}

	void AutoDetection::autoconfigure(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths, ProgressHandler *progressHandler)
	{
		spdlog::debug("Starting auto configure of all synths");
		// For all devices that are discoverable, run the find method
		for (auto synthHolder : allSynths) {
			if (progressHandler && progressHandler->shouldAbort()) break;
			if (synthHolder) {
				findSynth(synthHolder.get(), progressHandler);
			}
		}
		listenerToAllFound(allSynths);
		spdlog::debug("Auto configure of all synths done, notifying listeners");
		sendChangeMessage();
	}

	void AutoDetection::quickconfigure(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths, ProgressHandler *progressHandler)
	{
		spdlog::debug("Starting quick configure of all synths");
		// For all devices that are discoverable, run the find method
		for (auto &synthHolder : allSynths) {
			if (progressHandler && progressHandler->shouldAbort()) break;
			if (synthHolder) {
				auto synth = synthHolder.get();
				// Load the synthesizer setup from the settings file
				loadSettings(synth);
				// Hack - if the wait time is negative, don't autodetect. This needs to be replaced by some proper dynamic cast
				if (synthHolder->deviceDetectSleepMS() < 0) {
					continue;
				}
				if (progressHandler) progressHandler->setMessage(fmt::format("Checking saved connection for {}...", synth->getName()));
				bool connected = checkSynth(synth, progressHandler);
				if (progressHandler && progressHandler->shouldAbort()) break;
				if (!connected) {
					spdlog::warn(
						"No response from {} on channel {} of device {} - turn it on and check its saved connection, or use Find this synth if it has moved.",
							 synth->getName(), synth->channel().isValid() ? synth->channel().toOneBasedInt() : 0, synth->midiOutput().name.toStdString());
				}
				else {
					spdlog::info("Detected {} on channel {} of device {}",
						synth->getName(), synth->channel().toOneBasedInt(), synth->midiOutput().name.toStdString());
				}
		}
		}
		listenerToAllFound(allSynths);
		spdlog::debug("Quick configure of all synths done, notifying listeners");
		sendChangeMessage();
	}

	void AutoDetection::persistSetting(SimpleDiscoverableDevice *synth)
	{
		if (synth->channel().isValid()) {
			Settings::instance().set(midiSetupKey(synth, kChannel), fmt::format("{}", synth->channel().toZeroBasedInt()));
		}
		if (synth->midiInput().name.isNotEmpty()) {
			Settings::instance().set(midiSetupKey(synth, kInput), synth->midiInput().name.toStdString());
		}
		if (synth->midiOutput().name.isNotEmpty()) {
			Settings::instance().set(midiSetupKey(synth, kOutput), synth->midiOutput().name.toStdString());
		}
	}

	void AutoDetection::loadSettings(SimpleDiscoverableDevice *synth)
	{
		std::string input = Settings::instance().get(midiSetupKey(synth, kInput));
		synth->setInput(MidiController::instance()->getMidiInputByName(input));
		std::string output = Settings::instance().get(midiSetupKey(synth, kOutput));
		synth->setOutput(MidiController::instance()->getMidiOutputByName(output));

		synth->setChannel(MidiChannel::invalidChannel());
		int channel = savedChannel(synth);
		if (channel >= 0) synth->setChannel(MidiChannel::fromZeroBase(channel));
	}

	bool AutoDetection::hasSavedConnection(SimpleDiscoverableDevice *synth) {
		if (!synth) return false;
		auto &settings = Settings::instance();
		int channel = savedChannel(synth);
		return !settings.get(midiSetupKey(synth, kInput)).empty()
			&& !settings.get(midiSetupKey(synth, kOutput)).empty()
			&& channel >= 0 && channel < 16;
	}

	void AutoDetection::findSynth(SimpleDiscoverableDevice *synth, ProgressHandler *progressHandler) {
		// Hack - if the wait time is negative, don't autodetect. This needs to be replaced by some proper dynamic cast
		if (synth->deviceDetectSleepMS() < 0) {
			return;
		}

		if (progressHandler) {
			progressHandler->setMessage(fmt::format("Trying to detect {}...", synth->getName()));
		}

		auto locations = FindSynthOnMidiNetwork::detectSynth(*synth, progressHandler);
		if (progressHandler && progressHandler->shouldAbort()) return;
		synth->setWasDetected(!locations.empty());
		if (locations.size() > 0) {
			for (auto loc : locations) {
				spdlog::info("Found {} on channel {} replying on device {} when sending to {} on channel {}",
					synth->getName(), (loc.midiChannel.toOneBasedInt()),  loc.input.name.toStdString(), loc.output.name.toStdString(), loc.midiChannel.toOneBasedInt());
			}

			// Select the last location (the first one might be the "All" devices which we don't want to address the devices individually)
			size_t loc = locations.size() - 1;
			synth->setCurrentChannelZeroBased(locations[loc].input, locations[loc].output, locations[loc].midiChannel.toZeroBasedInt());

			// Additionally, we want to persist this knowhow in the user settings file!
			persistSetting(synth);
		}
		else {
			// Ups
			spdlog::error("No {} could be detected - is it turned on?", synth->getName());
		}
	}

	bool AutoDetection::checkSynth(SimpleDiscoverableDevice *synth, ProgressHandler *progressHandler) {
		// Hack - if the wait time is negative, don't autodetect. This needs to be replaced by some proper dynamic cast
		if (synth->deviceDetectSleepMS() < 0) {
			return false;
		}
		if (!synth->channel().isValid() || synth->channel().isOmni()
			|| synth->midiInput().identifier.isEmpty() || synth->midiOutput().identifier.isEmpty()) {
			synth->setWasDetected(false);
			return false;
		}

		// This is the fast version of the FindSynthOnMidiNetwork routine - just a single pass to see if the synth responds
		std::shared_ptr<IsSynth> callback = std::make_shared<IsSynth>(*synth);
		auto handler = MidiController::makeOneHandle();
		MidiController::instance()->addMessageHandler(handler, [weak = std::weak_ptr<IsSynth>(callback)](MidiInput *source, MidiMessage const &message) {
			if (auto detector = weak.lock()) detector->handleIncomingMidiMessage(source, message);
		});
		juce::ScopeGuard removeHandler { [handler]() { MidiController::instance()->removeMessageHandler(handler); } };
		ScopedMidiInput listener(*MidiController::instance(), synth->midiInput());
		auto output = MidiController::instance()->getMidiOutput(synth->midiOutput());
		if (!listener.isEnabled() || !output->isValid()) {
			synth->setWasDetected(false);
			return false;
		}

		// Send the detect message
		int deviceDetectId = 0x7f;
		if (synth->needsChannelSpecificDetection())
		{
			// Only use the channel when the device needs a channel as parameter. Most synths react on the 0x7f generic device value.
			deviceDetectId = synth->channel().toZeroBasedInt() & 0x7f;
		}
		auto detectMessage = synth->deviceDetect(deviceDetectId);
		// As of Dec 2020 the only Synth that needs more than one message for detection seems to be the Matrix 6, which is fast. 
		//TODO:  I cannot use the synth's sendBlockOfMessagesToSynth() here because I do not have a synth pointer. Smell?
		output->sendBlockOfMessagesFullSpeed(MidiHelpers::bufferFromMessages(detectMessage));

		// Sleep as long as the synth thinks is enough
		for (int remaining = synth->deviceDetectSleepMS(); remaining > 0; remaining -= 20) {
			if (progressHandler && progressHandler->shouldAbort()) return false;
			Thread::sleep(std::min(remaining, 20));
		}
		if (progressHandler && progressHandler->shouldAbort()) return false;

		// Check if we found it
		bool ok = false;
		for (auto found : callback->locations()) {
			if (found.input == synth->midiInput() && found.midiChannel.toZeroBasedInt() == synth->channel().toZeroBasedInt()) {
				ok = true;
				// Super special case - we might want to terminate the successful device detection with a special message sent to the same output as the detect message!
				MidiMessage endDetectMessage;
				if (synth->endDeviceDetect(endDetectMessage)) {
					MidiController::instance()->getMidiOutput(synth->midiOutput())->sendMessageNow(endDetectMessage);
				}
			}
		}
		synth->setWasDetected(ok);
		return ok;
	}

	void AutoDetection::listenerToAllFound(std::vector<std::shared_ptr<SimpleDiscoverableDevice>> &allSynths) {
		// Listen to all detected synths
		for (auto synth : allSynths) {
			if (synth && synth->wasDetected()) {
				MidiController::instance()->enableMidiInput(synth->midiInput());
			}
		}
	}

}
