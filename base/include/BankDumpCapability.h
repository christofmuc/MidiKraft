/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "MidiBankNumber.h"
#include "Patch.h"

namespace midikraft {

	// This synth supports some kind of Bank Dump (MidiMessages to Patches is M:N)
	class BankDumpCapability {
	public:
		struct HandshakeReply {
			bool isPartOfBankDump;
			std::vector<MidiMessage> handshakeReply;
		};

		struct FinishedReply {
			bool isFinished;
			std::vector<MidiMessage> handshakeReply;
		};

		virtual bool isBankDump(const MidiMessage& message) const = 0;
		virtual bool isBankDumpFinished(std::vector<MidiMessage> const &bankDump) const = 0;
		virtual HandshakeReply isMessagePartOfBankDump(const MidiMessage& message) const {
			return { isBankDump(message), {} };
		}
		virtual FinishedReply bankDumpFinishedWithReply(std::vector<MidiMessage> const &bankDump) const {
			return { isBankDumpFinished(bankDump), {} };
		}
		virtual TPatchVector patchesFromSysexBank(std::vector<MidiMessage> const& messages) const = 0;
	};

	// This means we can request a bank dump
	class BankDumpRequestCapability  {
	public:
		virtual std::vector<MidiMessage> requestBankDump(MidiBankNumber bankNo) const = 0;
	};

	// Implement this when the synth needs a specific message as banks and not just a list of program messages (e.g. DX7)
	class BankSendCapability {
	public:
		virtual std::vector<MidiMessage> createBankMessages(std::vector<std::vector<MidiMessage>> patches) = 0;
	};


}
