/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "DrawbarOrgan.h"

namespace midikraft {

	class Patch;

	class AdditiveCapability {
	public:
		virtual void selectRegistration(Patch *currentPatch, DrawbarOrgan::RegistrationDefinition selectedRegistration) = 0;
		virtual void selectHarmonics(Patch *currentPatch, std::string const &name, Additive::Harmonics const &selectedHarmonics) = 0;
	};

}
