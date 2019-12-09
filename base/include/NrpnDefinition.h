/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <string>
#include <map>

namespace midikraft {

	class NrpnDefinition {
	public:
		NrpnDefinition(int number, int min, int max, std::string const &name, int sysExIndex);
		NrpnDefinition(int number, int min, int max, std::string const &name, int sysExIndex, std::map<int, std::string> const &valueLookup);
		NrpnDefinition(int startNumber, int endNumber, int min, int max, std::string const &name, int sysExIndex);

		virtual std::string name() const;
		virtual std::string valueAsText(int value) const;
		virtual int valueFromText(std::string const &value) const;
		virtual int sysexIndex() const;
		virtual int numberOfValues() const;

		virtual bool matchesController(int controllerNumber) const;

		int min() const;
		int max() const;
		bool isOneOfThese(int nrpn) const;
		int number() const;
		bool isLookup() const;

	private:
		int number_;
		int endNumber_;
		int min_;
		int max_;
		int sysex_;
		std::string name_;
		std::map<int, std::string> valueLookup_;
	};

	class NRPNValue {
	public:
		NRPNValue(NrpnDefinition const &def, int value);

		NrpnDefinition const &def() const;
		int value() const;

	private:
		NrpnDefinition const &def_;
		int value_;
	};

}



