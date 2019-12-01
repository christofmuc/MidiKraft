#pragma once

#include <string>

class NamedDeviceCapability {
public:
	virtual std::string getName() const = 0;
};
