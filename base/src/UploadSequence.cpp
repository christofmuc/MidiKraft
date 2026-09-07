/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "UploadSequence.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace midikraft {

	UploadSequence::UploadSequence(
		UploadHandshakeCapability* handshake,
		std::vector<MidiMessage> messages,
		Send send,
		Finished finished)
		: handshake_(handshake), messages_(std::move(messages)), send_(std::move(send)), finished_(std::move(finished))
	{
	}

	void UploadSequence::start()
	{
		if (active_) return;
		active_ = true;
		if (messages_.empty()) {
			finish({ UploadResult::Status::ADAPTATION_ERROR, "empty_upload", "The upload produced no MIDI messages" });
			return;
		}
		advance();
	}

	void UploadSequence::advance()
	{
		while (active_ && currentMessage_ < messages_.size()) {
			bool expectsReply = false;
			try {
				expectsReply = handshake_ && handshake_->expectsUploadReply(messages_[currentMessage_]);
			}
			catch (const std::exception& ex) {
				finish({ UploadResult::Status::ADAPTATION_ERROR, "invalid_upload_handshake", ex.what(), currentMessage_ });
				return;
			}

			waitingForReply_ = expectsReply;
			usedAcknowledgement_ = usedAcknowledgement_ || expectsReply;
			if (!send({ messages_[currentMessage_] })) return;
			if (expectsReply) return;
			++currentMessage_;
		}

		if (active_) {
			finish({
				usedAcknowledgement_ ? UploadResult::Status::ACKNOWLEDGED : UploadResult::Status::SENT_WITHOUT_ACKNOWLEDGEMENT,
				{}, {}, currentMessage_, false
			});
		}
	}

	bool UploadSequence::send(const std::vector<MidiMessage>& messages)
	{
		try {
			if (!send_) throw std::runtime_error("No MIDI sender is available");
			send_(messages);
			return true;
		}
		catch (const std::exception& ex) {
			finish({ UploadResult::Status::TRANSPORT_ERROR, "midi_send_failed", ex.what(), currentMessage_, true });
			return false;
		}
	}

	void UploadSequence::handleIncomingMessage(const MidiMessage& message)
	{
		if (!active_ || !waitingForReply_ || !handshake_) return;

		UploadHandshakeReply reply;
		try {
			reply = handshake_->isMessagePartOfUploadReply(message, messages_[currentMessage_]);
		}
		catch (const std::exception& ex) {
			finish({ UploadResult::Status::ADAPTATION_ERROR, "invalid_upload_reply", ex.what(), currentMessage_, true });
			return;
		}

		switch (reply.status) {
		case UploadHandshakeReply::Status::UNRELATED:
			return;
		case UploadHandshakeReply::Status::CONTINUE:
			if (!reply.response.empty()) send(reply.response);
			return;
		case UploadHandshakeReply::Status::ACCEPTED:
			if (!reply.response.empty() && !send(reply.response)) return;
			waitingForReply_ = false;
			++currentMessage_;
			advance();
			return;
		case UploadHandshakeReply::Status::DEVICE_ERROR:
			finish({ UploadResult::Status::DEVICE_ERROR, reply.code, reply.message, currentMessage_, false });
			return;
		case UploadHandshakeReply::Status::ADAPTATION_ERROR:
			finish({ UploadResult::Status::ADAPTATION_ERROR, reply.code, reply.message, currentMessage_, true });
			return;
		}
	}

	void UploadSequence::timeout()
	{
		if (active_ && waitingForReply_) {
			finish({ UploadResult::Status::TIMEOUT, "upload_reply_timeout", "Timed out waiting for the synth to acknowledge the upload", currentMessage_, true });
		}
	}

	void UploadSequence::cancel()
	{
		if (active_) {
			finish({ UploadResult::Status::CANCELLED, "upload_cancelled", "Upload cancelled", currentMessage_, waitingForReply_ });
		}
	}

	void UploadSequence::finish(UploadResult result)
	{
		if (!active_) return;
		active_ = false;
		waitingForReply_ = false;
		if (finished_) finished_(result);
	}

}
