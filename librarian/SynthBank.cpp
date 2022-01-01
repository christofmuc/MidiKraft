/*
   Copyright (c) 2022 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SynthBank.h"

namespace midikraft {

	SynthBank::SynthBank(std::shared_ptr<Synth> synth, MidiBankNumber bank) :
		PatchList((String(synth->getName()) + "-bank-" + String(bank.toZeroBased())).toStdString(), synth->friendlyBankName(bank))
	{
	}

}

