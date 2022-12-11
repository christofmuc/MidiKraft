/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Patch.h"
#include "MidiProgramNumber.h"

namespace midikraft {

	class ProgramDumpCabability {
	public:
		struct HandshakeReply {
			bool isPartOfProgramDump;
			std::vector<MidiMessage> handshakeReply;
		};

		virtual std::vector<MidiMessage> requestPatch(int patchNo) const = 0;
		virtual bool isSingleProgramDump(const std::vector<MidiMessage>& messages) const = 0;
		virtual HandshakeReply isMessagePartOfProgramDump(const MidiMessage& message) const {
			return { isSingleProgramDump({ message }), {} };
		}
		virtual MidiProgramNumber getProgramNumber(const std::vector<MidiMessage> &message) const = 0;
		virtual std::shared_ptr<DataFile> patchFromProgramDumpSysex(const std::vector<MidiMessage>& message) const = 0;
		virtual std::vector<MidiMessage> patchToProgramDumpSysex(std::shared_ptr<DataFile> patch, MidiProgramNumber programNumber) const = 0;
	};

	class DefaultProgramPlaceInsteadOfEditBufferCapability {
	public:
		virtual MidiProgramNumber getDefaultProgramPlace() const = 0;
	};
}
