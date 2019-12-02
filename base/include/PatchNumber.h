/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "MidiProgramNumber.h"

#include <string>

namespace midikraft {

	class PatchNumber {
	public:
		PatchNumber() : programNumber_(MidiProgramNumber::fromZeroBase(0)) {}
		PatchNumber(MidiProgramNumber const &programNumber) : programNumber_(programNumber) { };
		virtual ~PatchNumber() = default;

		virtual std::string friendlyName() const = 0;

		MidiProgramNumber midiProgramNumber() const { return programNumber_; }

	protected:
		MidiProgramNumber programNumber_;
	};

}
