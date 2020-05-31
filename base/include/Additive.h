/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <vector>

namespace midikraft {

	class Additive {
	public:
		//! First value is the harmonic number (1-based, 1 is the fundamental), second value its amplitude in [0,1];
		typedef std::vector<std::pair<int, float>> Harmonics; 

		static std::vector<float> createSamplesFromHarmonics(Harmonics const &harmonics);
	};

}
