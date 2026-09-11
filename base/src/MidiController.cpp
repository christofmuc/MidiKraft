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

	SafeMidiOutput::SafeMidiOutput(MidiController *controller, std::shared_ptr<MidiOutput> midiOutput) :
		midiOut_(std::move(midiOutput)), controller_(controller)
	{
	}

	MidiMessage MidiController::makeTimeoutMessage()
	{
		// JUCE's default MidiMessage is an empty SysEx frame (F0 F7), not a
		// zero-length message. The raw-data constructor requires a positive length;
		// use the parser's invalid-status result to construct an empty message safely.
		const uint8 noStatus = 0;
		int bytesUsed = 0;
		return MidiMessage(&noStatus, 1, bytesUsed, 0, 0.0, false);
	}

	bool MidiController::isTimeoutMessage(const MidiMessage& message)
	{
		return message.getRawDataSize() == 0;
	}

	void SafeMidiOutput::sendMessageNow(const MidiMessage& message) {
		sendMessageNow(message, true);
	}

	void SafeMidiOutput::sendMessageNow(const MidiMessage& message, bool mirrorToSecondary) {
		if (midiOut_) {
			// Suppress empty sysex messages, they seem to confuse vintage hardware (e.g the Kawai K3 in particular)
			if (!(message.isSysEx() && message.getSysExDataSize() == 0)) {
				midiOut_->sendMessageNow(message);
				if (mirrorToSecondary) {
					controller_->midiMessageSent(message, midiOut_->getDeviceInfo());
				}
				else {
					controller_->logMidiMessage(message, midiOut_->getName(), true);
				}
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
			midiOut_->sendBlockOfMessagesNow(filtered);
			for (auto message : filtered) {
				auto m = message.getMessage();
				controller_->midiMessageSent(m, midiOut_->getDeviceInfo());
			}
		}
	}

	void SafeMidiOutput::sendBlockOfMessagesFullSpeed(const std::vector<MidiMessage>& buffer)
	{
		if (midiOut_) {
			for (const auto& message : buffer) {
				if (MidiHelpers::isEmptySysex(message)) continue;
				midiOut_->sendMessageNow(message);
				controller_->midiMessageSent(message, midiOut_->getDeviceInfo());
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
				controller_->midiMessageSent(message, midiOut_->getDeviceInfo());
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

	void MidiController::setSecondaryMidiOutput(juce::MidiDeviceInfo const& output)
	{
		std::function<void(const MidiMessage&)> sender;
		if (output.identifier.isNotEmpty()) {
			auto midiOutput = getMidiOutput(output);
			if (midiOutput->isValid()) {
				sender = [midiOutput](const MidiMessage& message) {
					midiOutput->sendMessageNow(message, false);
				};
			}
		}
		ScopedLock lock(secondaryMidiOutputLock_);
		secondaryMidiOutputIdentifier_ = output.identifier;
		secondaryMidiSender_ = std::move(sender);
	}

	bool MidiController::sendToSecondaryMidiOut(std::vector<MidiMessage> const& messages)
	{
		std::function<void(const MidiMessage&)> sender;
		{
			ScopedLock lock(secondaryMidiOutputLock_);
			sender = secondaryMidiSender_;
		}
		if (!sender) return false;
		for (const auto& message : messages) {
			if (message.getRawDataSize() > 0 && !MidiHelpers::isEmptySysex(message)) {
				sender(message);
			}
		}
		return true;
	}

	void MidiController::midiMessageSent(const MidiMessage& message, juce::MidiDeviceInfo const& output)
	{
		std::function<void(const MidiMessage&)> sender;
		{
			ScopedLock lock(secondaryMidiOutputLock_);
			// Device identifiers distinguish ports even when their display names match.
			if (output.identifier != secondaryMidiOutputIdentifier_) {
				sender = secondaryMidiSender_;
			}
		}
		if (sender) sender(message);
		// Filtering the log must never affect transmission.
		logMidiMessage(message, output.name, true);
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
						outputsOpen_[newOutput.identifier] = std::move(newDevice);
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
			safeOutputs_[midiOutput.identifier] = std::make_shared<SafeMidiOutput>(this, outputsOpen_[midiOutput.identifier]);
		}
		return safeOutputs_[midiOutput.identifier];
	}

	bool MidiController::enableMidiInput(juce::MidiDeviceInfo const& toEnable) {
		return inputOwnership_.enable(toEnable.identifier, [this, &toEnable]() { return startMidiInput(toEnable); });
	}

	void MidiController::disableMidiInput(juce::MidiDeviceInfo const &input) {
		inputOwnership_.disable(input.identifier, [this, &input]() { stopMidiInput(input); });
	}

	bool MidiController::acquireMidiInput(juce::MidiDeviceInfo const &input) {
		return inputOwnership_.acquire(input.identifier, [this, &input]() { return startMidiInput(input); });
	}

	void MidiController::releaseMidiInput(juce::MidiDeviceInfo const &input) {
		inputOwnership_.release(input.identifier, [this, &input]() { stopMidiInput(input); });
	}

	bool MidiController::startMidiInput(juce::MidiDeviceInfo const& toEnable) {
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

	void MidiController::stopMidiInput(juce::MidiDeviceInfo const& toDisable) {
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

	bool MidiController::isMidiInputEnabled(juce::MidiDeviceInfo const &input) const {
		return inputOwnership_.isEnabled(input.identifier);
	}

	// These methods handle callbacks from the midi device
	void MidiController::handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message) {
		logMidiMessage(message, source->getName(), false);

		// Call all currently registered handlers, but make sure to iterate over a copy of the list as it might get modified while the handlers run
		// First the new style handlers;
		std::vector<MidiCallback>newhandlers;
		auto now = Time::getMillisecondCounter();
		{
			ScopedLock lock(messageHandlerList_);
			for (auto& handler : messageHandlers_) {
				auto& entry = handler.second;
				entry.lastActivityMs = now;
				entry.timeoutState = TimeoutState::ACTIVE;
				entry.activityGeneration++;
				newhandlers.push_back(entry.callback);
			}
		}
		for (auto const &handler : newhandlers) {
			handler(source, message);
		}
	}

	void MidiController::handlePartialSysexMessage(MidiInput* source, const uint8* messageData, int numBytesSoFar, double timestamp) {
		// JUCE only delivers the completed MidiMessage after the terminating F7. Until then, each partial
		// packet is receive activity for handlers that opted into tracking long SysEx transfers.
		auto now = Time::getMillisecondCounter();
		{
			ScopedLock lock(messageHandlerList_);
			for (auto& handler : messageHandlers_) {
				auto& entry = handler.second;
				if (entry.timeoutActivity == TimeoutActivity::INCLUDE_PARTIAL_SYSEX) {
					entry.lastActivityMs = now;
					entry.timeoutState = TimeoutState::ACTIVE;
					entry.activityGeneration++;
				}
			}
		}

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
		std::set<juce::MidiDeviceInfo> inputDevices;
		{
			ScopedLock lock(inputOwnership_.lock_);
			inputDevices = currentInputs(false);
			std::vector<String> toDelete;
			for (auto input = inputsOpen_.begin(); input != inputsOpen_.end(); input++) {
				if (std::none_of(inputDevices.cbegin(), inputDevices.cend(), [input](juce::MidiDeviceInfo const& info) { return info.identifier == input->first;  })) {
					// Nope, that one is gone, closing it!
					spdlog::info("MIDI Input unplugged", input->second->getName());
					input->second.reset();
					inputOwnership_.disconnected(input->first);
					toDelete.push_back(input->first);
					dirty = true;
				}
			}

			for (auto del : toDelete) {
				inputsOpen_.erase(del);
			}
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
				{
					ScopedLock lock(secondaryMidiOutputLock_);
					if (output->first == secondaryMidiOutputIdentifier_) secondaryMidiSender_ = {};
				}
				// An in-flight secondary send retains the device until it returns.
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

		// Keep the handle and activity generation so a partial SysEx packet arriving while callbacks
		// are being prepared can invalidate a stale timeout.
		auto pendingTimeouts = collectExpiredHandlers(Time::getMillisecondCounter());

		// Use any available input as a placeholder for the timeout notification; handlers that compare names will ignore it
		MidiInput* placeholderInput = nullptr;
		{
			ScopedLock lock(inputOwnership_.lock_);
			if (!inputsOpen_.empty()) {
				placeholderInput = inputsOpen_.begin()->second.get();
			}
		}

		for (auto const& pending : pendingTimeouts) {
			// The locked transition to DISPATCHING is the linearization point between MIDI activity and
			// timeout delivery. Activity before it cancels PENDING; activity after it cannot revoke dispatch.
			auto handler = beginTimeoutDispatch(pending);
			if (handler) {
				spdlog::warn("MIDI controller timeout reached; dispatching timeout sentinel to handler");
				handler(placeholderInput, makeTimeoutMessage());
			}
		}
	}

	std::vector<MidiController::PendingTimeout> MidiController::collectExpiredHandlers(uint32 now)
	{
		std::vector<PendingTimeout> result;
		ScopedLock lock(messageHandlerList_);
		for (auto& handler : messageHandlers_) {
			auto& entry = handler.second;
			if (entry.timeoutMs > 0 && entry.timeoutState == TimeoutState::ACTIVE
				&& now - entry.lastActivityMs >= static_cast<uint32>(entry.timeoutMs)) {
				entry.timeoutState = TimeoutState::PENDING;
				result.push_back(PendingTimeout{ handler.first, entry.activityGeneration });
			}
		}
		return result;
	}

	MidiCallback MidiController::beginTimeoutDispatch(PendingTimeout const& pending)
	{
		ScopedLock lock(messageHandlerList_);
		auto handler = messageHandlers_.find(pending.handle);
		if (handler == messageHandlers_.end()) {
			return {};
		}

		auto& entry = handler->second;
		if (entry.timeoutState != TimeoutState::PENDING || entry.activityGeneration != pending.activityGeneration) {
			return {};
		}

		entry.timeoutState = TimeoutState::DISPATCHING;
		return entry.callback;
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

	void MidiController::addMessageHandler(HandlerHandle const &handle, MidiCallback handler, int timeoutMs, TimeoutActivity timeoutActivity) {
		ScopedLock lock(messageHandlerList_);
		messageHandlers_.insert(std::make_pair(handle, HandlerEntry{ handler, timeoutMs, Time::getMillisecondCounter(), TimeoutState::ACTIVE, timeoutActivity, 0 }));
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
