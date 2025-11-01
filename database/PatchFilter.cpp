/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchFilter.h"


namespace midikraft {

	PatchFilter::PatchFilter(std::map<std::string, std::weak_ptr<Synth>>& synth_list) : synths(synth_list) {
		initDefaults();
	}

	PatchFilter::PatchFilter(std::vector<std::shared_ptr<Synth>>&& synth_list) {
		for (auto const& synth : synth_list) {
			synths.emplace(synth->getName(), synth);
		}
		initDefaults();
	}

	PatchFilter::PatchFilter(std::vector<std::shared_ptr<Synth>>& synth_list) {
		for (auto const& synth : synth_list) {
			synths.emplace(synth->getName(), synth);
		}
		initDefaults();
	}

	void PatchFilter::initDefaults() {
		orderBy = PatchOrdering::Order_by_Import_id;
		onlyFaves = false;
		onlySpecifcType = false;
		typeID = 0;
		onlyUntagged = false;
		showHidden = false;
		showRegular = false;
		showUndecided = false;
		onlyDuplicateNames = false;
		andCategories = false;
	}

	void PatchFilter::turnOnAll()
	{
		onlyFaves = true;
		showHidden = true;
		showRegular = true;
		showUndecided = true;
	}

	bool operator!=(PatchFilter const& a, PatchFilter const& b)
	{
		// Check complex fields 
		for (auto const& asynth : a.synths) {
			if (b.synths.find(asynth.first) == b.synths.end()) {
				return true;
			}
		}
		for (auto const& bsynth : b.synths) {
			if (a.synths.find(bsynth.first) == a.synths.end()) {
				return true;
			}
		}

		if (a.categories != b.categories)
			return true;

		// Then check simple fields
		return a.importID != b.importID
			|| a.name != b.name
			|| a.listID != b.listID
			|| a.onlyFaves != b.onlyFaves
			|| a.onlySpecifcType != b.onlySpecifcType
			|| a.typeID != b.typeID
			|| a.showHidden != b.showHidden
			|| a.showRegular != b.showRegular
			|| a.showUndecided != b.showUndecided
			|| a.andCategories != b.andCategories
			|| a.onlyUntagged != b.onlyUntagged;
	}


}
