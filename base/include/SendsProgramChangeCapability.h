/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "MidiProgramNumber.h"

namespace midikraft {

	class SendsProgramChangeCapability {
	public:
		virtual void gotProgramChange(MidiProgramNumber newNumber) = 0;
		virtual MidiProgramNumber lastProgramChange() const = 0;
	};

}
