/*
   Copyright (c) 2022 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SynthBank.h"

#include "Logger.h"

namespace midikraft {

	SynthBank::SynthBank(std::shared_ptr<Synth> synth, MidiBankNumber bank, juce::Time lastSynced) :
		PatchList((String(synth->getName()) + "-bank-" + String(bank.toZeroBased())).toStdString(), synth->friendlyBankName(bank))
		, synth_(synth)
		, bankNo_(bank)
		, lastSynced_(lastSynced)
	{
	}

	void SynthBank::setPatches(std::vector<PatchHolder> patches)
	{
		for (auto patch : patches) {
			if (!validatePatchInfo(patch)) {
				return;
			}
		}
		PatchList::setPatches(patches);
	}

	void SynthBank::addPatch(PatchHolder patch) 
	{
		if (!validatePatchInfo(patch)) {
			return;
		}
		PatchList::addPatch(patch);
	}

	bool SynthBank::validatePatchInfo(PatchHolder patch) 
	{
		if (patch.smartSynth()->getName() != synth_->getName()) {
			SimpleLogger::instance()->postMessage("program error - list contains patches not for the synth of this bank, aborting");
			return false;
		}
		if (!patch.bankNumber().isValid() || (patch.bankNumber().toZeroBased() != bankNo_.toZeroBased())) {
			SimpleLogger::instance()->postMessage("program error - list contains patches for a different bank, aborting");
			return false;
		}
		if (patch.patchNumber().toOneBased() > synth_->numberOfPatches()) {
			SimpleLogger::instance()->postMessage("program error - list contains patches with non normalized program position, aborting");
			return false;
		}
		return true;
	}



}

