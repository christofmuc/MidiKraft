/*
   Copyright (c) 2022 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SynthBank.h"

#include "Logger.h"
#include "fmt/format.h"

namespace midikraft {

	SynthBank::SynthBank(std::shared_ptr<Synth> synth, MidiBankNumber bank, juce::Time lastSynced) :
		PatchList((String(synth->getName()) + "-bank-" + String(bank.toZeroBased())).toStdString(), synth->friendlyBankName(bank))
		, synth_(synth)
		, bankNo_(bank)
		, lastSynced_(lastSynced)
	{
	}

	std::string SynthBank::makeId(std::shared_ptr<Synth> synth, MidiBankNumber bank)
	{
		return (String(synth->getName()) + "-bank-" + String(bank.toZeroBased())).toStdString();
	}

	void SynthBank::setPatches(std::vector<PatchHolder> patches)
	{
		// Renumber the patches, the original patch information will not reflect the position 
		// of the patch in the bank, so it needs to be fixed.
		int i = 0;
		for (midikraft::PatchHolder& patch : patches) {
			patch.setBank(bankNo_);
			patch.setPatchNumber(MidiProgramNumber::fromZeroBaseWithBank(bankNo_, i++));
		}

		// Validate everything worked
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

	void SynthBank::changePatchAtPosition(MidiProgramNumber programPlace, PatchHolder patch)
	{
		auto currentList = patches();
		int position = programPlace.toZeroBased();
		if (position < currentList.size()) {
			// Check that we are not dropping a patch onto itself
			if (currentList[position].md5() != patch.md5()) {
				currentList[position] = patch;
				setPatches(currentList);
				dirtyPositions_.insert(position);
			}
		}
		else {
			jassertfalse;
		}
	}

	void SynthBank::copyListToPosition(MidiProgramNumber programPlace, PatchList const& list)
	{
		auto currentList = patches();
		int position = programPlace.toZeroBased();
		if (position < currentList.size()) {
			auto listToCopy = list.patches();
			int read_pos = 0;
			int write_pos = position;
			while (write_pos < std::min(currentList.size(), position + list.patches().size()) && read_pos < listToCopy.size()) {
				if (listToCopy[read_pos].synth()->getName() == synth_->getName()) {
					currentList[write_pos] = listToCopy[read_pos++];
					dirtyPositions_.insert(write_pos++);
				}
				else {
					SimpleLogger::instance()->postMessage(fmt::format("Skipping patch %s because it is for synth %s and cannot be put into the bank", listToCopy[read_pos].name(), listToCopy[read_pos].synth()->getName()));
					read_pos++;
				}
			}
			setPatches(currentList);
		}
		else {
			jassertfalse;
		}
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
		if (patch.patchNumber().isBankKnown() && patch.patchNumber().bank().toZeroBased() != bankNo_.toZeroBased()) {
			SimpleLogger::instance()->postMessage("program error - list contains patches with non normalized program position not matching current bank, aborting");
			return false;
		}
		return true;
	}



}

