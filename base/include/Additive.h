/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <map>
#include <vector>

namespace midikraft {

	//! A helper class to manage the harmonics of an Additive Synthesizer like the Kawai K3
	class Additive {
	public:
		//! First value is the harmonic number (1-based, 1 is the fundamental), second value its amplitude in [0,1];
		class Harmonics {
		public:
			void setHarmonic(int harmonicNumber, float value);

			std::map<int, float> harmonics() { return harmonics_; }
			std::map<int, float> const harmonics() const { return harmonics_; }

		private:
			std::map<int, float> harmonics_;
		};

		static std::vector<float> createSamplesFromHarmonics(Harmonics const &harmonics);
	};

}
