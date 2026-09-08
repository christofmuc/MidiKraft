/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "UploadOperation.h"

#include "Synth.h"

#include <spdlog/spdlog.h>

namespace midikraft {

	std::shared_ptr<UploadOperation> UploadOperation::start(
		Synth* synth,
		juce::MidiDeviceInfo midiInput,
		juce::MidiDeviceInfo midiOutput,
		UploadHandshakeCapability* handshake,
		std::vector<MidiMessage> messages,
		Finished finished)
	{
		auto operation = std::shared_ptr<UploadOperation>(new UploadOperation(
			synth, std::move(midiInput), std::move(midiOutput), handshake, std::move(messages), std::move(finished)));
		operation->begin();
		return operation;
	}

	UploadOperation::UploadOperation(
		Synth* synth,
		juce::MidiDeviceInfo midiInput,
		juce::MidiDeviceInfo midiOutput,
		UploadHandshakeCapability* handshake,
		std::vector<MidiMessage> messages,
		Finished finished)
		: synth_(synth), midiInput_(std::move(midiInput)), midiOutput_(std::move(midiOutput)),
		  handshake_(handshake), messages_(std::move(messages)), finished_(std::move(finished))
	{
	}

	UploadOperation::~UploadOperation()
	{
		cleanup();
	}

	void UploadOperation::begin()
	{
		if (!synth_) {
			complete({ UploadResult::Status::TRANSPORT_ERROR, "missing_synth", "No synth is available for upload" });
			return;
		}
		if (handshake_) {
			try {
				timeoutMs_ = handshake_->uploadReplyTimeoutMs();
			}
			catch (const std::exception& ex) {
				complete({ UploadResult::Status::ADAPTATION_ERROR, "invalid_upload_timeout", ex.what() });
				return;
			}
			if (timeoutMs_ <= 0) {
				complete({ UploadResult::Status::ADAPTATION_ERROR, "invalid_upload_timeout", "Upload reply timeout must be a positive number" });
				return;
			}
		}

		auto controller = MidiController::instance();
		if (!controller->enableMidiOutput(midiOutput_)) {
			complete({ UploadResult::Status::TRANSPORT_ERROR, "missing_midi_output", "The configured MIDI output is unavailable" });
			return;
		}
		if (handshake_) {
			if (midiInput_.identifier.isEmpty() || !controller->enableMidiInput(midiInput_)) {
				complete({ UploadResult::Status::TRANSPORT_ERROR, "missing_midi_input", "The configured MIDI input is unavailable for upload acknowledgements" });
				return;
			}
			handler_ = MidiController::makeOneHandle();
			auto weak = weak_from_this();
			controller->addMessageHandler(handler_, [weak](MidiInput* source, const MidiMessage& message) {
				if (auto operation = weak.lock()) operation->receive(source, message);
			});
			handlerRegistered_ = true;
		}

		auto weak = weak_from_this();
		sequence_ = std::make_unique<UploadSequence>(
			handshake_, std::move(messages_),
			[this](const std::vector<MidiMessage>& messages) {
				synth_->sendBlockOfMessagesToSynth(midiOutput_, messages);
			},
			[weak](const UploadResult& result) {
				if (auto operation = weak.lock()) operation->complete(result);
			});
		sequence_->start();
		updateDeadline();
	}

	void UploadOperation::receive(MidiInput* source, const MidiMessage& message)
	{
		if (!source || source->getDeviceInfo().identifier != midiInput_.identifier) return;

		auto weak = weak_from_this();
		MessageManager::callAsync([weak, message]() {
			if (auto operation = weak.lock()) {
				if (!operation->completed_ && operation->sequence_ && operation->sequence_->waitingForReply()) {
					auto status = operation->sequence_->handleIncomingMessage(message);
					// A related reply proves the device is making progress. Refresh the
					// deadline for CONTINUE and for an accepted step followed by another.
					if (!operation->completed_ && operation->sequence_->waitingForReply()
						&& status != UploadHandshakeReply::Status::UNRELATED) {
						operation->updateDeadline();
					}
				}
			}
		});
	}

	void UploadOperation::updateDeadline()
	{
		if (!sequence_ || !sequence_->active() || !sequence_->waitingForReply()) {
			stopTimer();
			return;
		}
		deadline_ = Time::getMillisecondCounter() + static_cast<uint32>(timeoutMs_);
		startTimer(20);
	}

	void UploadOperation::timerCallback()
	{
		if (sequence_ && sequence_->waitingForReply()
			&& static_cast<int32>(Time::getMillisecondCounter() - deadline_) >= 0) {
			sequence_->timeout();
		}
	}

	void UploadOperation::cancel()
	{
		if (sequence_) sequence_->cancel();
		else cleanup();
	}

	bool UploadOperation::active() const
	{
		return sequence_ && sequence_->active();
	}

	void UploadOperation::complete(const UploadResult& result)
	{
		if (completed_) return;
		completed_ = true;
		cleanup();
		if (finished_) finished_(result);
	}

	void UploadOperation::cleanup()
	{
		stopTimer();
		if (handlerRegistered_) {
			MidiController::instance()->removeMessageHandler(handler_);
			handlerRegistered_ = false;
		}
	}

}
