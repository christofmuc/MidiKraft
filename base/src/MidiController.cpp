/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MidiController.h"

#include "DiscoverableDevice.h"
#include "Logger.h"

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

	void SafeMidiOutput::sendBlockOfMessagesNow(const MidiBuffer& buffer) {
		if (midiOut_) {
			MidiBuffer::Iterator it(buffer);
			MidiMessage message;
			int position;
			MidiBuffer filtered;
			while (it.getNextEvent(message, position)) {
				// Suppress empty sysex messages, they seem to confuse vintage hardware (e.g the Kawai K3 in particular)
				if (message.isSysEx() && message.getSysExDataSize() == 0) continue;
				controller_->logMidiMessage(message, midiOut_->getName(), true);
				filtered.addEvent(message, position);
			}
			midiOut_->sendBlockOfMessagesNow(filtered);
		}
	}

	MidiController::MidiController()
	{
		if (instance_ != nullptr) {
			throw new std::runtime_error("This is a singleton, can't create twice");
		}
		instance_ = this;
	}

	MidiController::~MidiController()
	{
		// Remove all registered callbacks
		for (auto const &callback : callbacks_) {
			deviceManager.removeMidiInputCallback(callback.first, callback.second);
		}
	}

	MidiController * MidiController::instance()
	{
		if (instance_ == nullptr) {
			instance_ = new MidiController;
		}
		return instance_;
	}

	void MidiController::shutdown()
	{
		delete instance_;
	}

	void MidiController::logMidiMessage(const MidiMessage& message, const String& source, bool isOut) {
		if (midiLogFunction_) {
			midiLogFunction_(message, source, isOut);
		}
	}

	bool MidiController::enableMidiOutput(std::string const &newOutput)
	{
		// Check if it is already open
		if (outputsOpen_.find(newOutput) == outputsOpen_.end()) {
			auto devices = MidiOutput::getDevices();
			for (int index = 0; index < devices.strings.size(); index++) {
				if (devices.strings[index] == juce::String(newOutput)) {
					auto newDevice = MidiOutput::openDevice(index);
					if (newDevice) {
						// Take responsibility for the lifetime of the returned output
						newDevice.swap(outputsOpen_[newOutput]);
						return true;
					}
					SimpleLogger::instance()->postMessage("Failed to open MIDI device " + newOutput + ", maybe it is turned off or used by another software?");
				}
			}
		}
		return false;
	}

	void MidiController::setMidiLogFunction(std::function<void(const MidiMessage& message, const String& source, bool isOut)> logFunction) {
		midiLogFunction_ = logFunction;
	}

	std::shared_ptr<SafeMidiOutput> MidiController::getMidiOutput(std::string const &name)
	{
		if (safeOutputs_.find(name) == safeOutputs_.end()) {
			if (outputsOpen_.find(name) == outputsOpen_.end()) {
				// Lazy open
				if (!enableMidiOutput(name)) {
					safeOutputs_[name] = std::make_shared<SafeMidiOutput>(this, nullptr);
					return safeOutputs_[name];
				}
			}
			safeOutputs_[name] = std::make_shared<SafeMidiOutput>(this, outputsOpen_[name].get());
		}
		return safeOutputs_[name];
	}

	void MidiController::enableMidiInput(std::string const &newInput)
	{
		// Do not and never open a MIDI Input with an empty identifier, as this is a "catch all" function for JUCE, and you suddenly get duplicated messages everywhere!
		if (newInput.empty()) return;

		// Only enable and register once
		if (!deviceManager.isMidiInputEnabled(newInput)) {
			deviceManager.setMidiInputEnabled(newInput, true);
		}
		if (callbacks_.find(newInput) == callbacks_.end()) {
			deviceManager.addMidiInputCallback(newInput, this);
			callbacks_[newInput] = this;
		}
	}

	void MidiController::disableMidiInput(std::string const &input) {
		if (deviceManager.isMidiInputEnabled(input)) {
			deviceManager.setMidiInputEnabled(input, false);
		}
		if (callbacks_.find(input) == callbacks_.end()) {
			deviceManager.removeMidiInputCallback(input, callbacks_[input]);
			callbacks_.erase(input);
		}
	}

	// These methods handle callbacks from the midi device
	void MidiController::handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message) {
		if (!message.isActiveSense() && !message.isMidiClock()) {
			logMidiMessage(message, source->getName(), false);
		}

		// Call all currently registered handlers, but make sure to iterate over a copy of the list as it might get modified while the handlers run
		// First the new style handlers;
		std::vector < std::function<void(MidiInput *, MidiMessage const &)>> newhandlers;
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

	MidiController *MidiController::instance_ = nullptr;

}