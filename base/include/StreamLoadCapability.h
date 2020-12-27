/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Synth.h"

namespace midikraft {

	class StreamLoadCapability {
	public:
		enum class StreamType {
			EDIT_BUFFER_DUMP,
			BANK_DUMP,
		};

		virtual std::vector<MidiMessage> requestStreamElement(int elemNo, StreamType streamType) const = 0;
		virtual int numberOfStreamMessagesExpected(StreamType streamType) const = 0;
		virtual bool isMessagePartOfStream(MidiMessage const &message, StreamType streamType) const = 0;
		virtual bool isStreamComplete(std::vector<MidiMessage> const &messages, StreamType streamType) const = 0;
		virtual bool shouldStreamAdvance(std::vector<MidiMessage> const &messages, StreamType streamType) const = 0;
		virtual TPatchVector loadPatchesFromStream(std::vector<MidiMessage> const &streamDump) const = 0;
	};

}
