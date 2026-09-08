/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "UploadHandshakeCapability.h"

#include <functional>
#include <string>
#include <vector>

namespace midikraft {

	struct UploadResult {
		enum class Status {
			ACKNOWLEDGED,
			SENT_WITHOUT_ACKNOWLEDGEMENT,
			DEVICE_ERROR,
			TIMEOUT,
			CANCELLED,
			TRANSPORT_ERROR,
			ADAPTATION_ERROR,
			BUSY
		};

		Status status;
		std::string code;
		std::string message;
		size_t completedMessages = 0;
		bool outcomeUncertain = false;

		bool successful() const {
			return status == Status::ACKNOWLEDGED || status == Status::SENT_WITHOUT_ACKNOWLEDGEMENT;
		}
	};

	// Protocol state machine without MIDI or timer dependencies. UploadOperation
	// connects this to the live devices; keeping the sequence separate makes all
	// transition and error behavior deterministic and testable.
	class UploadSequence {
	public:
		using Send = std::function<void(const std::vector<MidiMessage>&)>;
		using Finished = std::function<void(const UploadResult&)>;

		UploadSequence(
			UploadHandshakeCapability* handshake,
			std::vector<MidiMessage> messages,
			Send send,
			Finished finished);

		void start();
		UploadHandshakeReply::Status handleIncomingMessage(const MidiMessage& message);
		void timeout();
		void cancel();

		bool active() const { return active_; }
		bool waitingForReply() const { return waitingForReply_; }
		size_t currentMessageIndex() const { return currentMessage_; }

	private:
		void advance();
		bool send(const std::vector<MidiMessage>& messages);
		void finish(UploadResult result);

		UploadHandshakeCapability* handshake_;
		std::vector<MidiMessage> messages_;
		Send send_;
		Finished finished_;
		size_t currentMessage_ = 0;
		bool active_ = false;
		bool waitingForReply_ = false;
		bool usedAcknowledgement_ = false;
	};

}
