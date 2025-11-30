/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include <map>
#include <set>

#include "DebounceTimer.h"

/*
inline bool operator <(const juce::MidiDeviceInfo &a, const juce::MidiDeviceInfo &b)
{
    return a.identifier < b.identifier;
}*/

namespace std
{
    template<> struct less<juce::MidiDeviceInfo>
    {
        bool operator() (const juce::MidiDeviceInfo& lhs, const juce::MidiDeviceInfo& rhs) const
        {
            return lhs.identifier < rhs.identifier;
        }
    };
}

namespace midikraft {

	// Forward declaration for the SafeMidiOutput class
	class MidiController;

	typedef std::function<void(MidiInput *source, MidiMessage const &message)> MidiCallback;
	typedef std::function<void(MidiInput *source, const uint8* data, int numBytesSoFar, double timestamp)> MidiDataCallback;

	class SafeMidiOutput {
	public:
		SafeMidiOutput(MidiController *controller, MidiOutput *midiOutput);

		void sendMessageNow(const MidiMessage& message);
		void sendMessageDebounced(const MidiMessage &message, int milliseconds);
		void sendBlockOfMessagesFullSpeed(const MidiBuffer& buffer);
		void sendBlockOfMessagesFullSpeed(const std::vector<MidiMessage>& buffer);
		void sendBlockOfMessagesThrottled(const std::vector<MidiMessage>& buffer, int millisecondsWait);

        juce::MidiDeviceInfo deviceInfo() const;
		std::string name() const;
		bool isValid() const;

	private:
		MidiOutput * midiOut_;
		MidiController *controller_;
		DebounceTimer debouncer_;
	};

	enum class MidiLogLevel {
		SYSEX_ONLY,
		ALL_BUT_REALTIME
	};

	// TODO - another example of bad naming. This is rather the "MidiDeviceManager"
	class MidiController : public ChangeBroadcaster, private MidiInputCallback, private Timer
	{
	public:
		typedef juce::Uuid HandlerHandle;
		static HandlerHandle makeOneHandle() { return juce::Uuid(); }
		static HandlerHandle makeNoneHandle() { return juce::Uuid::null(); }

		MidiController(); // Public for PyBind11
		
		// Timeout helpers (empty MidiMessage used as sentinel)
		static MidiMessage makeTimeoutMessage();
		static bool isTimeoutMessage(const MidiMessage& message);

		static MidiController *instance();
		static void shutdown(); // Call this last, and never call instance() again after this

		// Optional timeout: if timeoutMs > 0, handler receives makeTimeoutMessage() after that idle period
		void addMessageHandler(HandlerHandle const &handle, MidiCallback handler, int timeoutMs = -1);
		bool removeMessageHandler(HandlerHandle const &handle);
		
		void addPartialMessageHandler(HandlerHandle const& handle, MidiDataCallback handler);
		bool removePartialMessageHandler(HandlerHandle const& handle);

		void setMidiLogFunction(std::function<void(const MidiMessage& message, const String& source, bool)>);
		void logMidiMessage(const MidiMessage& message, const String& source, bool isOut);

		bool enableMidiOutput(juce::MidiDeviceInfo const &newOutput);
		std::shared_ptr<SafeMidiOutput> getMidiOutput(juce::MidiDeviceInfo const &name);
		bool enableMidiInput(juce::MidiDeviceInfo const &newInput);
		void disableMidiInput(juce::MidiDeviceInfo const &input);
        MidiDeviceInfo getMidiInputByIdentifier(String const &identifier);
        MidiDeviceInfo getMidiOutputByIdentifier(String const &identifier);

        MidiDeviceInfo getMidiInputByName(String const &name);
        MidiDeviceInfo getMidiOutputByName(String const &name);

		std::set<juce::MidiDeviceInfo> currentInputs(bool withHistory);
		std::set<juce::MidiDeviceInfo> currentOutputs(bool withHistory);

		void setMidiLogLevel(MidiLogLevel level);

	private:
		// Implementation of Callback
		virtual void handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message) override;
		virtual void handlePartialSysexMessage(MidiInput* source, const uint8* messageData, int numBytesSoFar, double timestamp) override;
		virtual void timerCallback() override;


		static MidiController *instance_;

		struct HandlerEntry {
			MidiCallback callback;
			int timeoutMs;
			uint32 lastActivityMs;
			bool timeoutTriggered;
		};

		// The list of handlers needs to be locked for thread-safe access
		CriticalSection messageHandlerList_;
		std::map<HandlerHandle, HandlerEntry> messageHandlers_;
		CriticalSection partialMessageHandlerList_;
		std::map<HandlerHandle, MidiDataCallback> partialHandlers_;

		std::set<juce::MidiDeviceInfo> knownInputs_, historyOfAllInputs_;
		std::set<juce::MidiDeviceInfo> knownOutputs_, historyOfAllOutpus_;
		std::map<String, std::unique_ptr<MidiOutput>> outputsOpen_;
		std::map<String, std::shared_ptr<SafeMidiOutput>> safeOutputs_;
		std::map<String, std::unique_ptr<MidiInput>> inputsOpen_;
		std::function<void(const MidiMessage& message, const String& source, bool)> midiLogFunction_;

		MidiLogLevel midiLogLevel_;
	};
	
}
