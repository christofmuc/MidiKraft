#include "NrpnDefinition.h"

NrpnDefinition::NrpnDefinition(int number, int min, int max, std::string const &name, int sysExIndex) : number_(number), min_(min), max_(max), name_(name), endNumber_(number), sysex_(sysExIndex)
{
}

NrpnDefinition::NrpnDefinition(int startNumber, int endNumber, int min, int max, std::string const &name, int sysExIndex) :
	NrpnDefinition(startNumber, min, max, name, sysExIndex)
{
	endNumber_ = endNumber;
}

NrpnDefinition::NrpnDefinition(int number, int min, int max, std::string const &name, int sysExIndex, std::map<int, std::string> const &valueLookup) :
	NrpnDefinition(number, min, max, name, sysExIndex)
{
	valueLookup_ = valueLookup;
}

std::string NrpnDefinition::name() const
{
	return name_;
}

std::string NrpnDefinition::valueAsText(int value) const
{
	if (valueLookup_.find(value) != valueLookup_.end()) {
		return valueLookup_.at(value);
	}
	return "unknown";
}

int NrpnDefinition::valueFromText(std::string const &value) const
{
	for (auto vl : valueLookup_) {
		if (vl.second == value) {
			return vl.first;
		}
	}
	return -1;
}

bool NrpnDefinition::matchesController(int controllerNumber) const
{
	return controllerNumber >= number_ && controllerNumber <= endNumber_;
}

int NrpnDefinition::min() const
{
	return min_;
}

int NrpnDefinition::max() const
{
	return max_;
}

bool NrpnDefinition::isOneOfThese(int nrpn) const
{
	return nrpn >= number_ && nrpn <= endNumber_;
}

int NrpnDefinition::sysexIndex() const
{
	return sysex_;
}

NRPNValue::NRPNValue(NrpnDefinition const &def, int value) :
	def_(def), value_(value)
{
}

NrpnDefinition const & NRPNValue::def() const
{
	return def_;
}

int NRPNValue::value() const
{
	return value_;
}

