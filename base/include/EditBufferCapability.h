/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Patch.h"

namespace midikraft {

	class EditBufferCapability {
	public:
		virtual std::vector<MidiMessage> requestEditBufferDump() const = 0;
		virtual bool isEditBufferDump(const std::vector<MidiMessage> & message) const = 0;
		virtual bool isMessagePartOfEditBuffer(const MidiMessage& message) const {
			return isEditBufferDump({ message });
		}

		virtual std::shared_ptr<DataFile> patchFromSysex(const std::vector<MidiMessage>& message) const = 0;
		virtual std::vector<MidiMessage> patchToSysex(std::shared_ptr<DataFile> patch) const = 0;

		virtual MidiMessage saveEditBufferToProgram(int programNumber) = 0;
	};

}
