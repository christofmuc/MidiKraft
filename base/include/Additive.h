/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <vector>

namespace midikraft {

	class Additive {
	public:
		typedef std::vector<std::pair<int, float>> Harmonics;

		static std::vector<float> createSamplesFromHarmonics(Harmonics const &harmonics);
	};

}
