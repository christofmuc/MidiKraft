/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "PatchHolder.h"
#include "AutomaticCategory.h"

namespace midikraft {

	class PatchInterchangeFormat {
	public:
		static std::vector<PatchHolder> load(std::map<std::string, std::shared_ptr<Synth>> activeSynths, std::string const &filename, std::shared_ptr<AutomaticCategory> detector);
		static void save(std::vector<PatchHolder> const &patches, std::string const &toFilename);
	};

}



