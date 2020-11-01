/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "PatchHolder.h"

namespace midikraft {

	class PatchInterchangeFormat {
	public:
		static std::vector<PatchHolder> load(std::shared_ptr<Synth> activeSynth, std::string const &filename);
	};

}



