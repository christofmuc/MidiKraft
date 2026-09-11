/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include <map>
#include <set>

#include "DebounceTimer.h"
#include "MidiInputOwnership.h"

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
#if defined(MIDIKRAFT_BUILD_TESTS)
	struct MidiControllerTestAccess;
#endif

	typedef std::function<void(MidiInput *source, MidiMessage const &message)> MidiCallback;
	typedef std::function<void(MidiInput *source, const uint8* data, int numBytesSoFar, double timestamp)> MidiDataCallback;

	class SafeMidiOutput {
	public:
		SafeMidiOutput(MidiController *controller, std::shared_ptr<MidiOutput> midiOutput);

		void sendMessageNow(const MidiMessage& message);
		void sendMessageDebounced(const MidiMessage &message, int milliseconds);
		void sendBlockOfMessagesFullSpeed(const MidiBuffer& buffer);
		void sendBlockOfMessagesFullSpeed(const std::vector<MidiMessage>& buffer);
		void sendBlockOfMessagesThrottled(const std::vector<MidiMessage>& buffer, int millisecondsWait);

        juce::MidiDeviceInfo deviceInfo() const;
		std::string name() const;
		bool isValid() const;

	private:
		friend class MidiController;
		void sendMessageNow(const MidiMessage& message, bool mirrorToSecondary);

		std::shared_ptr<MidiOutput> midiOut_;
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
		enum class TimeoutActivity {
			COMPLETE_MESSAGES_ONLY,
			INCLUDE_PARTIAL_SYSEX
		};

		static HandlerHandle makeOneHandle() { return juce::Uuid(); }
		static HandlerHandle makeNoneHandle() { return juce::Uuid::null(); }

		MidiController(); // Public for PyBind11
		
		// Timeout helpers (empty MidiMessage used as sentinel)
		static MidiMessage makeTimeoutMessage();
		static bool isTimeoutMessage(const MidiMessage& message);

		static MidiController *instance();
		static void shutdown(); // Call this last, and never call instance() again after this

		// Optional inactivity timeout: if timeoutMs > 0, handler receives makeTimeoutMessage() after that idle period.
		// Long SysEx transfers should use INCLUDE_PARTIAL_SYSEX so each incoming packet refreshes the timeout.
		void addMessageHandler(HandlerHandle const &handle, MidiCallback handler, int timeoutMs = -1,
			TimeoutActivity timeoutActivity = TimeoutActivity::COMPLETE_MESSAGES_ONLY);
		bool removeMessageHandler(HandlerHandle const &handle);
		
		void addPartialMessageHandler(HandlerHandle const& handle, MidiDataCallback handler);
		bool removePartialMessageHandler(HandlerHandle const& handle);

		void setMidiLogFunction(std::function<void(const MidiMessage& message, const String& source, bool)>);
		void logMidiMessage(const MidiMessage& message, const String& source, bool isOut);

		// Select/open on the UI thread. An empty device disables the secondary output.
		void setSecondaryMidiOutput(juce::MidiDeviceInfo const& output);
		// Returns false when no usable secondary output is selected. Never mirrors these messages again.
		bool sendToSecondaryMidiOut(std::vector<MidiMessage> const& messages);

		bool enableMidiOutput(juce::MidiDeviceInfo const &newOutput);
		std::shared_ptr<SafeMidiOutput> getMidiOutput(juce::MidiDeviceInfo const &name);
		bool enableMidiInput(juce::MidiDeviceInfo const &newInput);
		void disableMidiInput(juce::MidiDeviceInfo const &input);
		bool isMidiInputEnabled(juce::MidiDeviceInfo const &input) const;
		// Each successful scoped acquisition must be released. An input stays running
		// until both its final lease and any explicit enableMidiInput request are gone.
		bool acquireMidiInput(juce::MidiDeviceInfo const &input);
		void releaseMidiInput(juce::MidiDeviceInfo const &input);
        MidiDeviceInfo getMidiInputByIdentifier(String const &identifier);
        MidiDeviceInfo getMidiOutputByIdentifier(String const &identifier);

        MidiDeviceInfo getMidiInputByName(String const &name);
        MidiDeviceInfo getMidiOutputByName(String const &name);

		std::set<juce::MidiDeviceInfo> currentInputs(bool withHistory);
		std::set<juce::MidiDeviceInfo> currentOutputs(bool withHistory);

		void setMidiLogLevel(MidiLogLevel level);

	private:
		friend class SafeMidiOutput;
		void midiMessageSent(const MidiMessage& message, juce::MidiDeviceInfo const& output);

		CriticalSection secondaryMidiOutputLock_;
		String secondaryMidiOutputIdentifier_;
		std::function<void(const MidiMessage&)> secondaryMidiSender_;

#if defined(MIDIKRAFT_BUILD_TESTS)
		friend struct MidiControllerTestAccess;
#endif
		bool startMidiInput(juce::MidiDeviceInfo const &input);
		void stopMidiInput(juce::MidiDeviceInfo const &input);

		// Implementation of Callback
		virtual void handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message) override;
		virtual void handlePartialSysexMessage(MidiInput* source, const uint8* messageData, int numBytesSoFar, double timestamp) override;
		virtual void timerCallback() override;


		static MidiController *instance_;

		enum class TimeoutState {
			ACTIVE,
			PENDING,
			DISPATCHING
		};

		struct HandlerEntry {
			MidiCallback callback;
			int timeoutMs;
			uint32 lastActivityMs;
			TimeoutState timeoutState;
			TimeoutActivity timeoutActivity;
			uint64 activityGeneration;
		};

		struct PendingTimeout {
			HandlerHandle handle;
			uint64 activityGeneration;
		};

		std::vector<PendingTimeout> collectExpiredHandlers(uint32 now);
		MidiCallback beginTimeoutDispatch(PendingTimeout const& pending);

		// The list of handlers needs to be locked for thread-safe access
		CriticalSection messageHandlerList_;
		std::map<HandlerHandle, HandlerEntry> messageHandlers_;
		CriticalSection partialMessageHandlerList_;
		std::map<HandlerHandle, MidiDataCallback> partialHandlers_;

		std::set<juce::MidiDeviceInfo> knownInputs_, historyOfAllInputs_;
		std::set<juce::MidiDeviceInfo> knownOutputs_, historyOfAllOutpus_;
		std::map<String, std::shared_ptr<MidiOutput>> outputsOpen_;
		std::map<String, std::shared_ptr<SafeMidiOutput>> safeOutputs_;
		std::map<String, std::unique_ptr<MidiInput>> inputsOpen_;
		MidiInputOwnership inputOwnership_;
		std::function<void(const MidiMessage& message, const String& source, bool)> midiLogFunction_;

		MidiLogLevel midiLogLevel_;
	};

	// Temporarily listen on an input without stopping another synth's existing listener.
	template <typename Controller>
	class ScopedMidiInputFor {
	public:
		ScopedMidiInputFor(Controller &controller, MidiDeviceInfo input)
			: controller_(controller), input_(std::move(input)), enabled_(controller.acquireMidiInput(input_)) {}
		~ScopedMidiInputFor() {
			if (enabled_) controller_.releaseMidiInput(input_);
		}
		bool isEnabled() const { return enabled_; }
		ScopedMidiInputFor(ScopedMidiInputFor const &) = delete;
		ScopedMidiInputFor &operator=(ScopedMidiInputFor const &) = delete;
	private:
		Controller &controller_;
		MidiDeviceInfo input_;
		bool enabled_;
	};
	using ScopedMidiInput = ScopedMidiInputFor<MidiController>;
	
}
