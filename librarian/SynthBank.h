/*
   Copyright (c) 2022 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "PatchList.h"

namespace midikraft {

	class SynthBank : public PatchList {
	public:
		SynthBank(std::shared_ptr<Synth> synth, MidiBankNumber bank);
	};


}

