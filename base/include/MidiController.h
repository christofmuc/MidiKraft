/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include <map>

namespace midikraft {

	// Forward declaration for the SafeMidiOutput class
	class MidiController;

	typedef std::function<void(MidiInput *source, MidiMessage const &message)> MidiCallback;

	class SafeMidiOutput {
	public:
		SafeMidiOutput(MidiController *controller, MidiOutput *midiOutput);

		void sendMessageNow(const MidiMessage& message);
		void sendBlockOfMessagesNow(const MidiBuffer& buffer);

	private:
		MidiOutput * midiOut_;
		MidiController *controller_;
	};

	class MidiController : public MidiInputCallback
	{
	public:
		typedef juce::Uuid HandlerHandle;
		static HandlerHandle makeOneHandle() { return juce::Uuid(); }
		static HandlerHandle makeNoneHandle() { return juce::Uuid::null(); }

		MidiController();
		~MidiController();

		static MidiController *instance();

		//TODO - I think these should have an optional expiration date/timeout with a timeout handler, like when the expected response doesn't happen
		void addMessageHandler(HandlerHandle const &handle, MidiCallback handler);
		bool removeMessageHandler(HandlerHandle const &handle);

		void setMidiLogFunction(std::function<void(const MidiMessage& message, const String& source, bool)>);
		void logMidiMessage(const MidiMessage& message, const String& source, bool isOut);

		bool enableMidiOutput(std::string const &newOutput);
		SafeMidiOutput *getMidiOutput(std::string const &name);
		void enableMidiInput(std::string const &newInput);
		void disableMidiInput(std::string const &input);

		// Implementation of Callback
		virtual void handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message) override;

	private:
		static MidiController *instance_;
		AudioDeviceManager deviceManager;
		std::map<HandlerHandle, MidiCallback> messageHandlers_;
		std::map< std::string, std::unique_ptr<MidiOutput>> outputsOpen_;
		std::map< std::string, SafeMidiOutput *> safeOutputs_;
		std::map< std::string, MidiInputCallback *>  callbacks_;
		std::function<void(const MidiMessage& message, const String& source, bool)> midiLogFunction_;
	};
	
}
