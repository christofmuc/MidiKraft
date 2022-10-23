/*
   Copyright (c) 2022 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "PatchList.h"

namespace midikraft {

	class SynthBank : public PatchList {
	public:
		SynthBank(std::shared_ptr<Synth> synth, MidiBankNumber bank, juce::Time lastSynced);

		static std::string makeId(std::shared_ptr<Synth> synth, MidiBankNumber bank);

		// Override these to make sure they only contain patches for the synth, and have a proper program
		// location
		virtual void setPatches(std::vector<PatchHolder> patches) override;
		virtual void addPatch(PatchHolder patch) override;
		
		virtual void changePatchAtPosition(MidiProgramNumber programPlace, PatchHolder patch);
		
		void copyListToPosition(MidiProgramNumber programPlace, PatchList const& list);

		std::shared_ptr<Synth> synth() const
		{
			return synth_;
		}

		MidiBankNumber bankNumber() const
		{
			return bankNo_;
		}

		juce::Time lastSynced() const
		{
			return lastSynced_;
		}

		bool isPositionDirty(int position) const
		{
			return dirtyPositions_.find(position) != dirtyPositions_.end();
		}

		void clearDirty()
		{
			dirtyPositions_.clear();
		}

		static std::string friendlyBankName(std::shared_ptr<Synth> synth, MidiBankNumber bankNo);
		static int numberOfPatchesInBank(std::shared_ptr<Synth> synth, MidiBankNumber bankNo);
		static int numberOfPatchesInBank(std::shared_ptr<Synth> synth, int bankNoZeroBased);
		static int startIndexInBank(std::shared_ptr<Synth> synth, MidiBankNumber bankNo);

	private:
		bool validatePatchInfo(PatchHolder patch);

		std::shared_ptr<Synth> synth_;
		std::set<int> dirtyPositions_;
		MidiBankNumber bankNo_;
		juce::Time lastSynced_;
	};

}

