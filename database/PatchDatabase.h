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
#include "CategoryBitfield.h"

namespace midikraft {

	struct ImportInfo {
		std::string name; // The name of the import - TODO this can be matched to the name stored in the patch, except for edit buffer imports
		std::string description; // The nice display name of the import, this can contain e.g. the number of patches in this import in parentheses
		std::string id; // The database ID, as a unique identifier
	};

	class PatchDatabase {
	public:
		struct PatchFilter {
			std::map<std::string, std::weak_ptr<Synth>> synths;
			std::string importID;
			std::string name;
			bool onlyFaves;
			bool onlySpecifcType;
			int typeID;
			bool showHidden;
			bool onlyUntagged;
			std::set<Category> categories;
		};

		struct CategoryDefinition {
			int id;
			bool isActive;
			std::string name;
			Colour color;
		};

		enum UpdateChoice {
			UPDATE_NAME = 1,
			UPDATE_CATEGORIES = 2,
			UPDATE_HIDDEN = 4,
			UPDATE_DATA = 8,
			UPDATE_FAVORITE = 16,
			UPDATE_ALL = UPDATE_NAME | UPDATE_CATEGORIES | UPDATE_HIDDEN | UPDATE_DATA | UPDATE_FAVORITE
		};

		PatchDatabase(); // Default location
		PatchDatabase(std::string const &databaseFile); // Specific file
		~PatchDatabase();

		std::string getCurrentDatabaseFileName() const;
		bool switchDatabaseFile(std::string const &newDatabaseFile);

		int getPatchesCount(PatchFilter filter);
		std::vector<PatchHolder> getPatches(PatchFilter filter, int skip, int limit);
		void getPatchesAsync(PatchFilter filter, std::function<void(std::vector<PatchHolder> const &)> finished, int skip, int limit);

		size_t mergePatchesIntoDatabase(std::vector<PatchHolder> &patches, std::vector<PatchHolder> &outNewPatches, ProgressHandler *progress, unsigned updateChoice);
		std::vector<ImportInfo> getImportsList(Synth *activeSynth) const;
		bool putPatch(PatchHolder const &patch);
		bool putPatches(std::vector<PatchHolder> const &patches);

		int deletePatches(PatchFilter filter);
		int reindexPatches(PatchFilter filter);

		std::string makeDatabaseBackup(std::string const &suffix);
		void makeDatabaseBackup(File backupFileToCreate);

		std::vector<CategoryDefinition> getCategories() const;
		std::shared_ptr<AutomaticCategory> getCategorizer();
		int getNextBitindex();
		void updateCategories(std::vector<CategoryDefinition> const &newdefs);
		void setAutocategorizationRules(std::string const &jsonDefinition);

		// Convenience functions
		static PatchFilter allForSynth(std::shared_ptr<Synth> synth);

		// For backward compatibility
		static std::string generateDefaultDatabaseLocation();

	private:
		class PatchDataBaseImpl;
		std::unique_ptr<PatchDataBaseImpl> impl;
		ThreadPool pool_;
	};

}
