/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "Additive.h"

#include <string>
#include <map>
#include <string>

namespace midikraft {

	class Drawbar {
	public:
		enum Color {
			BROWN, WHITE, BLACK
		};

		Drawbar(std::string const &name, Color color, std::string const &pitch, std::string const &traditional_name, int harmonic_number) :
			name_(name), color_(color), pitch_(pitch), traditional_name_(traditional_name), harmonic_number_(harmonic_number) {};

		std::string name_, pitch_, traditional_name_;
		Color color_;
		int harmonic_number_;
	};

	class DrawbarOrgan {
	public:
		typedef std::pair<std::string, Additive::Harmonics> RegistrationDefinition;
		static std::vector<RegistrationDefinition> popularRegistrations;
		static std::vector<RegistrationDefinition> pipeOrganStops;

		static Additive::Harmonics HarmonicsFromRegistration(std::string const &registrationString);

		static std::vector<Drawbar> hammondDrawbars();
	};

}
