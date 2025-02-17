/*
   Copyright (c) 2022 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SynthBank.h"

#include "Logger.h"
#include "Capability.h"
#include "HasBanksCapability.h"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace midikraft {

	SynthBank::SynthBank(std::string const& name, std::shared_ptr<Synth> synth, MidiBankNumber bank) :
		PatchList(name)
		, synth_(synth)
		, bankNo_(bank)
	{
	}

	SynthBank::SynthBank(std::string const& id, std::string const& name, std::shared_ptr<Synth> synth, MidiBankNumber bank) :
		PatchList(id, name)
		, synth_(synth)
		, bankNo_(bank)
	{
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
		// In case the bank was not full (could be a brand new user bank), fill it up with empty holders
		for (size_t j = patches.size(); static_cast<int>(j) < bankNo_.bankSize(); j++) {
			auto initPatch = midikraft::PatchHolder(synth_, nullptr, nullptr);
			initPatch.setBank(bankNo_);
			auto patchNo = MidiProgramNumber::fromZeroBaseWithBank(bankNo_, (int)j);
			initPatch.setPatchNumber(patchNo);
			if (initPatch.name().empty())
			{
				if (j < patches.size()) {
					initPatch.setName(patches[j].smartSynth()->friendlyProgramAndBankName(bankNo_, patchNo));
				}
			}
			patches.push_back(initPatch);
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

	std::string SynthBank::targetBankName() const {
		return friendlyBankName(synth_, bankNo_);
	}

	bool SynthBank::isWritable() const {
		// ROM banks can only be defined with the newer BankDescriptorsCapability
		auto descriptors = synth_->getCapability<midikraft::HasBankDescriptorsCapability>();
		if (descriptors) {
			auto banks = descriptors->bankDescriptors();
			if (bankNo_.toZeroBased() < static_cast<int>(banks.size())) {
				return !banks[bankNo_.toZeroBased()].isROM;
			}
		}
		// We actually don't know...
		return true;
	}

	void SynthBank::fillWithPatch(PatchHolder initPatch) {
		auto copy = patches();
		bool modified = false;
		for (auto patch = copy.begin(); patch != copy.end(); patch++) {
			if (patch->patch() == nullptr) {
				// This is an empty button, put out Patch into it!
				auto old = *patch;
				*patch = initPatch;
				patch->setBank(old.bankNumber());
				patch->setPatchNumber(old.patchNumber());
				modified = true;
				dirtyPositions_.insert(old.patchNumber().toZeroBasedDiscardingBank());
			}
		}
		if (modified) {
			setPatches(copy);
		}
	}

	void SynthBank::changePatchAtPosition(MidiProgramNumber programPlace, PatchHolder patch)
	{
        updatePatchAtPosition(programPlace, patch);
		int position = programPlace.toZeroBasedDiscardingBank();
		if (position < static_cast<int>(patches().size())) {
			fillWithPatch(patch);
		}
		else {
			jassertfalse;
		}
	}

	void SynthBank::updatePatchAtPosition(MidiProgramNumber programPlace, PatchHolder patch)
	{
		auto currentList = patches();
		int position = programPlace.toZeroBasedDiscardingBank();
		if (position < static_cast<int>(currentList.size())) {
            if (currentList[position].md5() != patch.md5() || currentList[position].name() != patch.name()) {
				dirtyPositions_.insert(position);
            }
			currentList[position] = patch;
			setPatches(currentList);
		}
		else {
			jassertfalse;
		}
	}

	void SynthBank::copyListToPosition(MidiProgramNumber programPlace, PatchList const& list)
	{
		auto currentList = patches();
		int position = programPlace.toZeroBasedDiscardingBank();
		if (position < static_cast<int>(currentList.size())) {
			auto listToCopy = list.patches();
			int read_pos = 0;
			int write_pos = position;
			while (write_pos < static_cast<int>(std::min(currentList.size(), position + list.patches().size())) && read_pos < static_cast<int>(listToCopy.size())) {
				if (listToCopy[read_pos].synth()->getName() == synth_->getName()) {
					currentList[write_pos] = listToCopy[read_pos++];
					dirtyPositions_.insert(write_pos++);
				}
				else {
					spdlog::info("Skipping patch {} because it is for synth {} and cannot be put into the bank", listToCopy[read_pos].name(), listToCopy[read_pos].synth()->getName());
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
		if (patch.smartSynth() && patch.smartSynth()->getName() != synth_->getName()) {
			spdlog::error("program error - list contains patches not for the synth of this bank, aborting");
			return false;
		}
		if (!patch.bankNumber().isValid() || (patch.bankNumber().toZeroBased() != bankNo_.toZeroBased())) {
			spdlog::error("program error - list contains patches for a different bank, aborting");
			return false;
		}
		if (patch.patchNumber().isBankKnown() && patch.patchNumber().bank().toZeroBased() != bankNo_.toZeroBased()) {
			spdlog::error("program error - list contains patches with non normalized program position not matching current bank, aborting");
			return false;
		}
		return true;
	}

	std::string SynthBank::friendlyBankName(std::shared_ptr<Synth> synth, MidiBankNumber bankNo)
	{
		auto descriptors = synth->getCapability<midikraft::HasBankDescriptorsCapability>();
		if (descriptors) {
			auto banks = descriptors->bankDescriptors();
			if (bankNo.toZeroBased() < static_cast<int>(banks.size())) {
				return banks[bankNo.toZeroBased()].name;
			}
			else {
				return fmt::format("out of range bank %d", bankNo.toZeroBased());
			}
		}
		auto banks = synth->getCapability<midikraft::HasBanksCapability>();
		if (banks) {
			return banks->friendlyBankName(bankNo);
		}
		return fmt::format("invalid bank %d", bankNo.toZeroBased());
	}

	int SynthBank::numberOfPatchesInBank(std::shared_ptr<Synth> synth, MidiBankNumber bankNo)
	{
		return numberOfPatchesInBank(synth, bankNo.toZeroBased());
	}

	int SynthBank::numberOfPatchesInBank(std::shared_ptr<Synth> synth, int bankNo)
	{
		auto descriptors = synth->getCapability<midikraft::HasBankDescriptorsCapability>();
		if (descriptors) {
			auto banks = descriptors->bankDescriptors();
			if (bankNo < static_cast<int>(banks.size())) {
				return banks[bankNo].size;
			}
			else {
				jassertfalse;
				spdlog::error("Program error: Bank number out of range in numberOfPatchesInBank in Librarian");
				return 0;
			}
		}
		auto banks = synth->getCapability<midikraft::HasBanksCapability>();
		if (banks) {
			return banks->numberOfPatches();
		}
		jassertfalse;
		spdlog::error("Program error: Trying to determine number of patches for synth without HasBanksCapability");
		return 0;
	}

	int SynthBank::startIndexInBank(std::shared_ptr<Synth> synth, MidiBankNumber bankNo)
	{
		auto descriptors = synth->getCapability<midikraft::HasBankDescriptorsCapability>();
		if (descriptors) {
			auto banks = descriptors->bankDescriptors();
			if (bankNo.toZeroBased() < static_cast<int>(banks.size())) {
				int index = 0;
				for (int b = 0; b < bankNo.toZeroBased(); b++) {
					index += banks[b].size;
				}
				return index;
			}
			else {
				jassertfalse;
				spdlog::error("Program error: Bank number out of range in numberOfPatchesInBank in Librarian");
				return 0;
			}
		}
		auto banks = synth->getCapability<midikraft::HasBanksCapability>();
		if (banks) {
			return bankNo.toZeroBased() * banks->numberOfPatches();
		}
		jassertfalse;
		spdlog::error("Program error: Trying to determine number of patches for synth without HasBanksCapability");
		return 0;
	}

	ActiveSynthBank::ActiveSynthBank(std::shared_ptr<Synth> synth, MidiBankNumber bank, juce::Time lastSynced) :
		SynthBank(ActiveSynthBank::makeId(synth, bank), friendlyBankName(synth, bank), synth, bank)
		, lastSynced_(lastSynced)
	{
	}


	std::string ActiveSynthBank::makeId(std::shared_ptr<Synth> synth, MidiBankNumber bank)
	{
		return (String(synth->getName()) + "-bank-" + String(bank.toZeroBased())).toStdString();
	}

}

