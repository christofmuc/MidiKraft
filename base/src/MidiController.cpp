/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MidiController.h"

#include "DiscoverableDevice.h"
#include "Logger.h"

#include "MidiHelpers.h"

#include <spdlog/spdlog.h>
#include "SpdLogJuce.h"

namespace midikraft {

	SafeMidiOutput::SafeMidiOutput(MidiController *controller, MidiOutput *midiOutput) :
		midiOut_(midiOutput), controller_(controller)
	{
	}

	void SafeMidiOutput::sendMessageNow(const MidiMessage& message) {
		if (midiOut_) {
			// Suppress empty sysex messages, they seem to confuse vintage hardware (e.g the Kawai K3 in particular)
			if (!(message.isSysEx() && message.getSysExDataSize() == 0)) {
				controller_->logMidiMessage(message, midiOut_->getName(), true);
				midiOut_->sendMessageNow(message);
			}
		}
	}

	void SafeMidiOutput::sendMessageDebounced(const MidiMessage &message, int milliseconds)
	{
		debouncer_.callDebounced([this, message]() {
			sendMessageNow(message);
		}, 
			milliseconds);
	}

	void SafeMidiOutput::sendBlockOfMessagesFullSpeed(const MidiBuffer& buffer) {
		if (midiOut_) {
			MidiBuffer filtered = MidiHelpers::removeEmptySysexMessages(buffer);
			for (auto message : filtered) {
				auto m = message.getMessage();
				controller_->logMidiMessage(m, midiOut_->getName(), true);
			}
			midiOut_->sendBlockOfMessagesNow(filtered);
		}
	}

	void SafeMidiOutput::sendBlockOfMessagesFullSpeed(const std::vector<MidiMessage>& buffer)
	{
		if (midiOut_) {
			for (const auto& message : buffer) {
				if (MidiHelpers::isEmptySysex(message)) continue;
				midiOut_->sendMessageNow(message);
				controller_->logMidiMessage(message, midiOut_->getName(), true);
			}
		}
	}

	void SafeMidiOutput::sendBlockOfMessagesThrottled(const std::vector<MidiMessage>& buffer, int millisecondsWait) {
		//TODO - this blocks the UI thread, but I don't want any logic to continue right now here.
		if (midiOut_) {
			for (const auto& message : buffer) {
				if (MidiHelpers::isEmptySysex(message)) continue;
				Thread::sleep(millisecondsWait);
				midiOut_->sendMessageNow(message);
				controller_->logMidiMessage(message, midiOut_->getName(), true);
			}
		}
	}

	juce::MidiDeviceInfo SafeMidiOutput::deviceInfo() const
	{
		if (midiOut_) {
			return midiOut_->getDeviceInfo();
		}
		return juce::MidiDeviceInfo();
	}

	std::string SafeMidiOutput::name() const
	{
		return isValid() ? midiOut_->getName().toStdString() : "invalid_midi_out";
	}

	bool SafeMidiOutput::isValid() const
	{
		return midiOut_ != nullptr && midiOut_->getIdentifier().isNotEmpty();
	}

	MidiController::MidiController() : midiLogLevel_(MidiLogLevel::SYSEX_ONLY)
	{
		if (instance_ != nullptr) {
			throw std::runtime_error("This is a singleton, can't create twice");
		}
		instance_ = this;

		// Find the current list of connected MIDI ports
		knownOutputs_ = currentOutputs(false);
		knownInputs_ = currentInputs(false);

		startTimer(500); // Do start a timer monitoring new devices from appearing and known devices from disappearing, as there is USB after all
	}

	MidiController * MidiController::instance()
	{
		if (instance_ == nullptr) {
			instance_ = new MidiController();
		}
		return instance_;
	}

	void MidiController::shutdown()
	{
		delete instance_;
		instance_ = nullptr;
	}

	void MidiController::logMidiMessage(const MidiMessage& message, const String& source, bool isOut) {
		if (midiLogFunction_) {
			bool doLog = false;
			switch (midiLogLevel_) {
			case MidiLogLevel::SYSEX_ONLY:
				doLog = message.isSysEx();
				break;
			case MidiLogLevel::ALL_BUT_REALTIME:
				doLog = !message.isActiveSense() && !message.isMidiClock();
				break;
			default:
				doLog = true;
			}
			if (doLog)
			{
				midiLogFunction_(message, source, isOut);
			}
		}
	}

	bool MidiController::enableMidiOutput(juce::MidiDeviceInfo const &newOutput)
	{
		if (newOutput.identifier.isEmpty()) return false;

		// Check if it is already open
		if (outputsOpen_.find(newOutput.identifier) == outputsOpen_.end()) {
			auto devices = MidiOutput::getAvailableDevices();
			for (const auto& device : devices) {
				if (device.identifier == newOutput.identifier) {
					auto newDevice = juce::MidiOutput::openDevice(device.identifier);
					if (newDevice) {
						// Take responsibility for the lifetime of the returned output
						newDevice.swap(outputsOpen_[newOutput.identifier]);
						spdlog::trace("MIDI output {} opened with ID {}", newOutput.name, device.identifier);
						return true;
					}
					spdlog::error("MIDI output {} could not be opened, maybe it is turned off or used by another software?", newOutput.name);
					return false;
				}
			}
			spdlog::info("Could not find MIDI output {}, device disconnected?", newOutput.name);
			return false;
		}
		return true;
	}

	void MidiController::setMidiLogFunction(std::function<void(const MidiMessage& message, const String& source, bool isOut)> logFunction) {
		midiLogFunction_ = logFunction;
	}

	std::shared_ptr<SafeMidiOutput> MidiController::getMidiOutput(juce::MidiDeviceInfo const &midiOutput)
	{
		if (safeOutputs_.find(midiOutput.identifier) == safeOutputs_.end() || !safeOutputs_[midiOutput.identifier]->isValid()) {
			if (outputsOpen_.find(midiOutput.identifier) == outputsOpen_.end()) {
				// Lazy open
				if (!enableMidiOutput(midiOutput)) {
					// Create a safe empty wrapper
					safeOutputs_[midiOutput.identifier] = std::make_shared<SafeMidiOutput>(this, nullptr);
					return safeOutputs_[midiOutput.identifier];
				}
			}
			safeOutputs_[midiOutput.identifier] = std::make_shared<SafeMidiOutput>(this, outputsOpen_[midiOutput.identifier].get());
		}
		return safeOutputs_[midiOutput.identifier];
	}

	bool MidiController::enableMidiInput(juce::MidiDeviceInfo const& toEnable)
	{
		// Do not and never open a MIDI Input with an empty identifier, as this is a "catch all" function for JUCE, and you suddenly get duplicated messages everywhere!
		if (toEnable.identifier.isEmpty()) return false;

		for (const auto& device : MidiInput::getAvailableDevices()) {
			if (device.identifier == toEnable.identifier) {
				// Has this device already been opened?
				if (inputsOpen_.find(toEnable.identifier) == inputsOpen_.end()) {
					inputsOpen_[toEnable.identifier] = juce::MidiInput::openDevice(device.identifier, this);
					if (inputsOpen_[toEnable.identifier]) {
						inputsOpen_[toEnable.identifier]->start();
						spdlog::trace("MIDI input {} opened with ID {}", toEnable.name, device.identifier);
						return true;
					}
					else {
						inputsOpen_.erase(toEnable.identifier);
						spdlog::error("MIDI input {} could not be opened, maybe it is locked by another software running?", toEnable.name);
						return false;
					}
				}
				else {
					// Make sure it is still open and running. This could happen when e.g. a MIDI USB device is removed and inserted back in
					inputsOpen_[toEnable.identifier]->start();
					spdlog::trace("MIDI input device {} restarted, id is {}", toEnable.name, toEnable.identifier);
					return true;
				}
			}
		}
		spdlog::error("MIDI input {} could not be opened, not found. Please plugin/turn on the device.", toEnable.name);
		return false;
	}

	void MidiController::disableMidiInput(juce::MidiDeviceInfo const& toDisable) {
		if (toDisable.identifier.isEmpty()) return;

		// Has this device ever been opened?
		if (inputsOpen_.find(toDisable.identifier) == inputsOpen_.end()) {
			spdlog::error("MIDI input {} never was opened, can't disable! Program error?", toDisable.name);
		}
		else {
			spdlog::trace("MIDI input {} stopped, id {}", toDisable.name, toDisable.identifier);
			inputsOpen_[toDisable.identifier]->stop();
		}
	}

	// These methods handle callbacks from the midi device
	void MidiController::handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message) {
		logMidiMessage(message, source->getName(), false);

		// Call all currently registered handlers, but make sure to iterate over a copy of the list as it might get modified while the handlers run
		// First the new style handlers;
		std::vector<MidiCallback>newhandlers;
		{
			ScopedLock lock(messageHandlerList_);
			for (auto const &handler : messageHandlers_) {
				newhandlers.push_back(handler.second);
			}
		}
		for (auto const &handler : newhandlers) {
			handler(source, message);
		}
	}

	void MidiController::handlePartialSysexMessage(MidiInput* source, const uint8* messageData, int numBytesSoFar, double timestamp) {
		// Call all currently registered handlers, but make sure to iterate over a copy of the list as it might get modified while the handlers run
		// First the new style handlers;
		std::vector<MidiDataCallback> newhandlers;
		{
			ScopedLock lock(partialMessageHandlerList_);
			for (auto const& handler : partialHandlers_) {
				newhandlers.push_back(handler.second);
			}
		}
		for (auto const& handler : newhandlers) {
			handler(source, messageData, numBytesSoFar, timestamp);
		}
	}

	//TODO This can be replaced by a MidiDeviceListConnection now
	void MidiController::timerCallback()
	{
		 // Check if all devices are still there. We won't get notified, so better be safe than sorry and let's count them!
		bool dirty = false;
		
		// Check if all open devices are still there, else stop them and delete them
		//TODO Could I use the new set knownDevices_ here to an advantage?
		std::vector<String> toDelete;
		auto inputDevices = currentInputs(false);
		for (auto input = inputsOpen_.begin(); input != inputsOpen_.end(); input++) {
			if (std::none_of(inputDevices.cbegin(), inputDevices.cend(), [input](juce::MidiDeviceInfo const& info) { return info.identifier == input->first;  })) {
				// Nope, that one is gone, closing it!
				spdlog::info("MIDI Input unplugged", input->second->getName());
				input->second.reset();
				toDelete.push_back(input->first);
				dirty = true;
			}
		}

		for (auto del : toDelete) {
			inputsOpen_.erase(del);
		}

		// Check if any new devices came up
		if (inputDevices != knownInputs_) {
			for (auto input : inputDevices) {
				if (knownInputs_.find(input) == knownInputs_.end()) {
					spdlog::info("MIDI Input {} connected", input.name);
					dirty = true;
				}
			}
		}
		knownInputs_ = inputDevices;
		historyOfAllInputs_.insert(knownInputs_.begin(), knownInputs_.end());

		// Now the same for the Output devices
		std::vector<String> toDeleteOutput;
		auto outputDevices = currentOutputs(false);
		for (auto output = outputsOpen_.begin(); output != outputsOpen_.end(); output++) {
			if (std::none_of(outputDevices.cbegin(), outputDevices.cend(), [output](juce::MidiDeviceInfo const& info) { return info.identifier == output->first;  })) {
				spdlog::info("MIDI Output {} unplugged", output->second->getName());
				output->second.reset();
				toDeleteOutput.push_back(output->first);
				dirty = true;
			}
		}

		for (auto del : toDeleteOutput) {
			outputsOpen_.erase(del);
			safeOutputs_.erase(del);
		}

		// Check if any new devices came up
		if (outputDevices!= knownOutputs_) {
			for (auto output: outputDevices) {
				if (knownOutputs_.find(output) == knownOutputs_.end()) {
					spdlog::info("MIDI output {} connected", output.name);
					dirty = true;
				}
			}
		}
		knownOutputs_ = outputDevices;
		historyOfAllOutpus_.insert(knownOutputs_.begin(), knownOutputs_.end());

		if (dirty) {
			spdlog::debug("Detected change in MIDI device list, notifying listeners");
			sendChangeMessage();
		}
	}

	std::set<juce::MidiDeviceInfo> MidiController::currentInputs(bool withHistory)
	{
		std::set<juce::MidiDeviceInfo> inputDevices;
		auto availableInputs = MidiInput::getAvailableDevices();
		std::for_each(availableInputs.begin(), availableInputs.end(), [&](MidiDeviceInfo device) {inputDevices.emplace(device); });
		if (withHistory) {
			inputDevices.insert(historyOfAllInputs_.begin(), historyOfAllInputs_.end());
		}
		return inputDevices;
	}

	std::set<juce::MidiDeviceInfo> MidiController::currentOutputs(bool withHistory)
	{
		std::set<juce::MidiDeviceInfo> outputDevices;
		auto availableOuputs = MidiOutput::getAvailableDevices();
		std::for_each(availableOuputs.begin(), availableOuputs.end(), [&](MidiDeviceInfo device) {outputDevices.emplace(device); });
		if (withHistory) {
			outputDevices.insert(historyOfAllOutpus_.begin(), historyOfAllOutpus_.end());
		}
		return outputDevices;
	}

	void MidiController::setMidiLogLevel(MidiLogLevel level) {
		midiLogLevel_ = level;
	}

    juce::MidiDeviceInfo MidiController::getMidiOutputByIdentifier(const String &identifier)
    {
        for (auto const& output : currentOutputs(true))
        {
            if (output.identifier == identifier)
            {
                return output;
            }
        }
        return MidiDeviceInfo();
    }

    juce::MidiDeviceInfo MidiController::getMidiInputByIdentifier(const String &identifier)
    {
        for (auto const& output : currentInputs(true))
        {
            if (output.identifier == identifier)
            {
                return output;
            }
        }
        return MidiDeviceInfo();
    }

	juce::MidiDeviceInfo MidiController::getMidiOutputByName(const String &name)
	{
		for (auto const& output : currentOutputs(true))
		{
			if (output.name == name)
			{
				return output;
			}
		}
		return MidiDeviceInfo(name, "");
	}

	juce::MidiDeviceInfo MidiController::getMidiInputByName(const String &name)
	{
		for (auto const& output : currentInputs(true))
		{
			if (output.name == name)
			{
				return output;
			}
		}
		return MidiDeviceInfo(name, "");
	}

	void MidiController::addMessageHandler(HandlerHandle const &handle, MidiCallback handler) {
		ScopedLock lock(messageHandlerList_);
		messageHandlers_.insert(std::make_pair(handle, handler));
	}

	bool MidiController::removeMessageHandler(HandlerHandle const &handle) {
		ScopedLock lock(messageHandlerList_);
		if (messageHandlers_.find(handle) != messageHandlers_.end()) {
			messageHandlers_.erase(handle);
			return true;
		}
		jassertfalse;
		return false;
	}

	void MidiController::addPartialMessageHandler(HandlerHandle const& handle, MidiDataCallback handler) {
		ScopedLock lock(partialMessageHandlerList_);
		partialHandlers_.insert(std::make_pair(handle, handler));
	}

	bool MidiController::removePartialMessageHandler(HandlerHandle const& handle) {
		ScopedLock lock(partialMessageHandlerList_);
		if (partialHandlers_.find(handle) != partialHandlers_.end()) {
			partialHandlers_.erase(handle);
			return true;
		}
		jassertfalse;
		return false;
	}

	MidiController *MidiController::instance_ = nullptr;

}