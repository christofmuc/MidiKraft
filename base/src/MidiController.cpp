/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MidiController.h"

#include "DiscoverableDevice.h"

namespace midikraft {

	SafeMidiOutput::SafeMidiOutput(MidiController *controller, MidiOutput *midiOutput) :
		midiOut_(midiOutput), controller_(controller)
	{
	}

	void SafeMidiOutput::sendMessageNow(const MidiMessage& message) {
		if (midiOut_) {
			controller_->logMidiMessage(message, midiOut_->getName(), true);
			midiOut_->sendMessageNow(message);
		}
	}

	void SafeMidiOutput::sendBlockOfMessagesNow(const MidiBuffer& buffer) {
		if (midiOut_) {
			MidiBuffer::Iterator it(buffer);
			MidiMessage message;
			int position;
			while (it.getNextEvent(message, position)) {
				controller_->logMidiMessage(message, midiOut_->getName(), true);
			}
			midiOut_->sendBlockOfMessagesNow(buffer);
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
		for (auto callback : callbacks_) {
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
					jassert(false); // Failed to open existing Midi Device
				}
			}
		}
		return false;
	}

	void MidiController::setMidiLogFunction(std::function<void(const MidiMessage& message, const String& source, bool isOut)> logFunction) {
		midiLogFunction_ = logFunction;
	}

	SafeMidiOutput * MidiController::getMidiOutput(std::string const &name)
	{
		if (safeOutputs_.find(name) == safeOutputs_.end()) {
			if (outputsOpen_.find(name) == outputsOpen_.end()) {
				// Lazy open
				if (!enableMidiOutput(name)) {
					safeOutputs_[name] = new SafeMidiOutput(this, nullptr);
					return safeOutputs_[name];
				}
			}
			safeOutputs_[name] = new SafeMidiOutput(this, outputsOpen_[name].get());
		}
		return safeOutputs_[name];
	}

	void MidiController::enableMidiInput(std::string const &newInput)
	{
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
		for (auto handler : messageHandlers_) {
			newhandlers.push_back(handler.second);
		}
		for (auto handler : newhandlers) {
			handler(source, message);
		}
	}

	void MidiController::addMessageHandler(HandlerHandle const &handle, MidiCallback handler) {
		messageHandlers_.insert(std::make_pair(handle, handler));
	}

	bool MidiController::removeMessageHandler(HandlerHandle const &handle) {
		if (messageHandlers_.find(handle) != messageHandlers_.end()) {
			messageHandlers_.erase(handle);
			return true;
		}
		return false;
	}

	MidiController *MidiController::instance_ = nullptr;

}