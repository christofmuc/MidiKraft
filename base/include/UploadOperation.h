/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"
#include "MidiController.h"
#include "UploadSequence.h"

#include <memory>

namespace midikraft {

	class Synth;

	class UploadOperation : private Timer, public std::enable_shared_from_this<UploadOperation> {
	public:
		using Finished = UploadSequence::Finished;

		static std::shared_ptr<UploadOperation> start(
			Synth* synth,
			juce::MidiDeviceInfo midiInput,
			juce::MidiDeviceInfo midiOutput,
			UploadHandshakeCapability* handshake,
			std::vector<MidiMessage> messages,
			Finished finished);

		~UploadOperation() override;
		void cancel();
		bool active() const;

	private:
		UploadOperation(
			Synth* synth,
			juce::MidiDeviceInfo midiInput,
			juce::MidiDeviceInfo midiOutput,
			UploadHandshakeCapability* handshake,
			std::vector<MidiMessage> messages,
			Finished finished);

		void begin();
		void receive(MidiInput* source, const MidiMessage& message);
		void updateDeadline();
		void complete(const UploadResult& result);
		void cleanup();
		void timerCallback() override;

		Synth* synth_;
		juce::MidiDeviceInfo midiInput_;
		juce::MidiDeviceInfo midiOutput_;
		UploadHandshakeCapability* handshake_;
		std::vector<MidiMessage> messages_;
		Finished finished_;
		std::unique_ptr<UploadSequence> sequence_;
		MidiController::HandlerHandle handler_;
		int timeoutMs_ = 0;
		uint32 deadline_ = 0;
		bool handlerRegistered_ = false;
		bool completed_ = false;
	};

}
