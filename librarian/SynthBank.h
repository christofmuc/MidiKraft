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

		// Override these to make sure they only contain patches for the synth, and have a proper program
		// location
		virtual void setPatches(std::vector<PatchHolder> patches);
		virtual void addPatch(PatchHolder patch);

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

	private:
		bool validatePatchInfo(PatchHolder patch);

		std::shared_ptr<Synth> synth_;
		MidiBankNumber bankNo_;
		juce::Time lastSynced_;
	};

}

