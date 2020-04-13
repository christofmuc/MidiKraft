/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "MidiBankNumber.h"
#include "Patch.h"

namespace midikraft {

	class BankDumpCapability {
	public:
		virtual std::vector<MidiMessage> requestBankDump(MidiBankNumber bankNo) const = 0;
		virtual bool isBankDump(const MidiMessage& message) const = 0;
		virtual bool isBankDumpFinished(std::vector<MidiMessage> const &bankDump) const = 0;
		virtual TPatchVector patchesFromSysexBank(const MidiMessage& message) const = 0;
	};

}
