/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <memory>
#include <vector>

#include "Synth.h"

#include "ProgressHandler.h"
#include "PatchHolder.h"

namespace midikraft {

	struct ImportInfo {
		std::string name; // The name of the import - TODO this can be matched to the name stored in the patch, except for edit buffer imports
		std::string description; // The nice display name of the import, this can contain e.g. the number of patches in this import in parantheses
		std::string id; // The database ID, as a unique identifier
	};

	class PatchDatabase {
	public:
		struct PatchFilter {
			Synth *activeSynth;
			std::string importID;
			bool onlyFaves;
			bool showHidden;
			bool onlyUntagged;
			std::set<Category> categories;
		};

		PatchDatabase();
		~PatchDatabase();

		int getPatchesCount(PatchFilter filter);
		std::vector<PatchHolder> getPatches(PatchFilter filter, int skip, int limit);
		void getPatchesAsync(PatchFilter filter, std::function<void(std::vector<PatchHolder> const &)> finished, int skip, int limit);

		size_t mergePatchesIntoDatabase(Synth *activeSynth, std::vector<PatchHolder> &patches, std::vector<PatchHolder> &outNewPatches, ProgressHandler *progress);
		std::vector<ImportInfo> getImportsList(Synth *activeSynth) const;
		bool putPatch(Synth *activeSynth, PatchHolder const &patch);
		bool putPatches(Synth *activeSynth, std::vector<PatchHolder> const &patches);

		void runMigration(Synth * activeSynth);

	private:
		class PatchDataBaseImpl;
		std::unique_ptr<PatchDataBaseImpl> impl;
		ThreadPool pool_;
	};

}
