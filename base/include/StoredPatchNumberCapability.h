/*
   Copyright (c) 2022 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "MidiProgramNumber.h"

namespace midikraft {

	//! Implement this to signal that a synth's patch has (potentially) a stored patch number
	//! Potentially, because the synth might also have edit buffer dumps
	class StoredPatchNumberCapability {
	public:
		virtual bool hasStoredPatchNumber() const = 0;
		virtual MidiProgramNumber getStoredPatchNumber() const = 0;
		virtual void setStoredPatchNumber(MidiProgramNumber newNumber) const = 0;
	};

}



