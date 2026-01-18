/*
   Copyright (c) 2022 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "PatchList.h"

namespace midikraft {

	class SynthBank : public PatchList {
	public:
		SynthBank(std::string const& name, std::shared_ptr<Synth> synth, MidiBankNumber bank);
        virtual ~SynthBank() override = default;

		// Override these to make sure they only contain patches for the synth, and have a proper program
		// location
		virtual void setPatches(std::vector<PatchHolder> patches) override;
		virtual void addPatch(PatchHolder patch) override;

		// Synth bank specific code
		std::string targetBankName() const;
		// Test if this is a ROM bank
		bool isWritable() const;

		virtual void fillWithPatch(PatchHolder patch);
		
		virtual void changePatchAtPosition(MidiProgramNumber programPlace, PatchHolder patch);
		virtual void updatePatchAtPosition(MidiProgramNumber programPlace, PatchHolder patch);

		virtual bool isUserBank() const = 0;
		virtual bool isActiveSynthBank() const = 0;
		
		void copyListToPosition(MidiProgramNumber programPlace, PatchList const& list);

		std::shared_ptr<Synth> synth() const
		{
			return synth_;
		}

		MidiBankNumber bankNumber() const
		{
			return bankNo_;
		}

		bool isDirty() const {
			return !dirtyPositions_.empty();
		}

		bool isPositionDirty(int position) const
		{
			return dirtyPositions_.find(position) != dirtyPositions_.end();
		}

		void clearDirty()
		{
			dirtyPositions_.clear();
		}

		int patchCapacity() {
			return numberOfPatchesInBank(synth_, bankNo_);
		}

		static std::string friendlyBankName(std::shared_ptr<Synth> synth, MidiBankNumber bankNo);
		static int numberOfPatchesInBank(std::shared_ptr<Synth> synth, MidiBankNumber bankNo);
		static int numberOfPatchesInBank(std::shared_ptr<Synth> synth, int bankNoZeroBased);
		static int startIndexInBank(std::shared_ptr<Synth> synth, MidiBankNumber bankNo);

	protected:
		SynthBank(std::string const& id, std::string const& name, std::shared_ptr<Synth> synth, MidiBankNumber bank);

	private:
		bool validatePatchInfo(PatchHolder patch);

		std::shared_ptr<Synth> synth_;
		std::set<int> dirtyPositions_;
		MidiBankNumber bankNo_;
		
	};

	class UserBank : public SynthBank {
	public:
		UserBank(std::string const& id, std::string const& name, std::shared_ptr<Synth> synth, MidiBankNumber bank) 
			: SynthBank(id, name, synth, bank) 
		{}

		bool isUserBank() const override { return true; }
		bool isActiveSynthBank() const override { return false; }
	};

	class ActiveSynthBank : public SynthBank
	{
	public:
		ActiveSynthBank(std::shared_ptr<Synth> synth, MidiBankNumber bank, juce::Time lastSynced);

		static std::string makeId(std::shared_ptr<Synth> synth, MidiBankNumber bank);

		juce::Time lastSynced() const
		{
			return lastSynced_;
		}

		bool isUserBank() const override { return false; }
		bool isActiveSynthBank() const override { return true; }

	private:
		juce::Time lastSynced_;
	};

}

