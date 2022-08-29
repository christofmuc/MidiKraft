#pragma once

#include "MidiBankNumber.h"

namespace midikraft {

	// This is the old way of doing things, pre Orm 1.17 you needed to implement the following three functions
	class HasBanksCapability {
	public:
		virtual int numberOfBanks() const = 0;
		virtual int numberOfPatches() const = 0;
		virtual std::string friendlyBankName(MidiBankNumber bankNo) const = 0;
	};

	// From 1.17 on, you would implement this capability returning a more interesting descriptor
	struct BankDescriptor {
		int bankNo;
		std::string friendlyBankName;
		int numPatchesInBank;
		bool isWritable;
	};

	class HasBankDescriptorsCapability {
	public:
		virtual std::vector<BankDescriptor> bankDescriptors() const = 0;
	};

}

