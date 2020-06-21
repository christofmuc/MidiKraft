#pragma once

#include "MidiBankNumber.h"

namespace midikraft {

	class HasBanksCapability {
	public:
		virtual int numberOfBanks() const = 0;
		virtual int numberOfPatches() const = 0;
		virtual std::string friendlyBankName(MidiBankNumber bankNo) const = 0;
	};

}

