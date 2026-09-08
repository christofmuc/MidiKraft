/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include <string>
#include <vector>

namespace midikraft {

	struct UploadHandshakeReply {
		enum class Status {
			UNRELATED,
			CONTINUE,
			ACCEPTED,
			DEVICE_ERROR,
			ADAPTATION_ERROR
		};

		Status status = Status::UNRELATED;
		std::vector<MidiMessage> response;
		std::string code;
		std::string message;
	};

	class UploadHandshakeCapability {
	public:
		virtual ~UploadHandshakeCapability() = default;

		// Most handshake protocols acknowledge every message. Adaptations can opt
		// individual messages (for example a trailing program change) out.
		virtual bool expectsUploadReply(const MidiMessage&) const { return true; }

		virtual UploadHandshakeReply isMessagePartOfUploadReply(
			const MidiMessage& message,
			const MidiMessage& sentMessage) const = 0;

		virtual int uploadReplyTimeoutMs() const { return 5000; }
	};

}
