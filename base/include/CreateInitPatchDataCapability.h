/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "Synth.h"

namespace midikraft {

	class CreateInitPatchDataCapability {
	public:
		virtual Synth::PatchData createInitPatch() = 0;
	};

}
