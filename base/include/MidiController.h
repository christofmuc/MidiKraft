/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include <map>
#include <set>

#include "DebounceTimer.h"

namespace midikraft {

	// Forward declaration for the SafeMidiOutput class
	class MidiController;

	typedef std::function<void(MidiInput *source, MidiMessage const &message)> MidiCallback;

	class SafeMidiOutput {
	public:
		SafeMidiOutput(MidiController *controller, MidiOutput *midiOutput);

		void sendMessageNow(const MidiMessage& message);
		void sendMessageDebounced(const MidiMessage &message, int milliseconds);
		void sendBlockOfMessagesNow(const MidiBuffer& buffer);

		bool isValid() const;

	private:
		MidiOutput * midiOut_;
		MidiController *controller_;
		DebounceTimer debouncer_;
	};

	// TODO - another example of bad naming. This is rather the "MidiDeviceManager"
	class MidiController : public ChangeBroadcaster, private MidiInputCallback, private Timer
	{
	public:
		typedef juce::Uuid HandlerHandle;
		static HandlerHandle makeOneHandle() { return juce::Uuid(); }
		static HandlerHandle makeNoneHandle() { return juce::Uuid::null(); }

		MidiController(); // Public for PyBind11

		static MidiController *instance();
		static void shutdown(); // Call this last, and never call instance() again after this

		//TODO - I think these should have an optional expiration date/timeout with a timeout handler, like when the expected response doesn't happen
		void addMessageHandler(HandlerHandle const &handle, MidiCallback handler);
		bool removeMessageHandler(HandlerHandle const &handle);

		void setMidiLogFunction(std::function<void(const MidiMessage& message, const String& source, bool)>);
		void logMidiMessage(const MidiMessage& message, const String& source, bool isOut);

		bool enableMidiOutput(std::string const &newOutput);
		std::shared_ptr<SafeMidiOutput> getMidiOutput(std::string const &name);
		bool enableMidiInput(std::string const &newInput);
		void disableMidiInput(std::string const &input);

		std::set<std::string> currentInputs(bool withHistory);
		std::set<std::string> currentOutputs(bool withHistory);

	private:
		// Implementation of Callback
		virtual void handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message) override;
		virtual void timerCallback() override;


		static MidiController *instance_;

		// The list of handlers needs to be locked for thread-safe access
		CriticalSection messageHandlerList_;
		std::map<HandlerHandle, MidiCallback> messageHandlers_;

		std::set<std::string> knownInputs_, historyOfAllInputs_;
		std::set<std::string> knownOutputs_, historyOfAllOutpus_;
		std::map< std::string, std::unique_ptr<MidiOutput>> outputsOpen_;
		std::map< std::string, std::shared_ptr<SafeMidiOutput>> safeOutputs_;
		std::map< std::string, std::unique_ptr<MidiInput>> inputsOpen_;
		std::function<void(const MidiMessage& message, const String& source, bool)> midiLogFunction_;
	};
	
}
