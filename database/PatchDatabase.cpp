/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchDatabase.h"

#include "Patch.h"
#include "Logger.h"
#include "PatchHolder.h"

#include "JsonSchema.h"
#include "JsonSerialization.h"

#include "ProgressHandler.h"

#include <iostream>
#include <boost/format.hpp>

#include "SQLiteCpp/Database.h"
#include "SQLiteCpp/Statement.h"
#include "SQLiteCpp/Transaction.h"

namespace midikraft {

	const int SCHEMA_VERSION = 5;
	/* History */
	/* 1 - Initial schema */
	/* 2 - adding hidden flag (aka deleted) */
	/* 3 - adding type integer to patch (to differentiate voice, patch, layer, tuning...) */
	/* 4 - forgot to migrate existing data NULL to 0 */
	/* 5 - adding the table categories to track which bit index is used for which tag */

	class PatchDatabase::PatchDataBaseImpl {
	public:
		PatchDatabase::PatchDataBaseImpl() : db_(generateDatabaseLocation().toStdString().c_str(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE) {
			createSchema();
		}

		String generateDatabaseLocation() {
			auto knobkraft = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("KnobKraft");
			if (!knobkraft.exists()) {
				knobkraft.createDirectory();
			}
			return knobkraft.getChildFile("SysexDatabaseOfAllPatches.db3").getFullPathName();
		}

		void migrateSchema(int currentVersion) {
			if (currentVersion < 2) {
				SQLite::Transaction transaction(db_);
				db_.exec("ALTER TABLE patches ADD COLUMN hidden INTEGER");
				db_.exec("UPDATE schema_version SET number = 2");
				transaction.commit();
			}
			if (currentVersion < 3) {
				SQLite::Transaction transaction(db_);
				db_.exec("ALTER TABLE patches ADD COLUMN type INTEGER");
				db_.exec("UPDATE schema_version SET number = 3");
				transaction.commit();
			}
			if (currentVersion < 4) {
				SQLite::Transaction transaction(db_);
				db_.exec("UPDATE patches SET type = 0 WHERE type is NULL");
				db_.exec("UPDATE schema_version SET number = 4");
				transaction.commit();
			}
			if (currentVersion < 5) {
				SQLite::Transaction transaction(db_);
				db_.exec("CREATE TABLE categories (bitIndex INTEGER, name TEXT, color TEXT, active INTEGER)");
				// Colors from http://colorbrewer2.org/#type=qualitative&scheme=Set3&n=12
				db_.exec("INSERT INTO categories VALUES (0, 'Lead', 'ff8dd3c7', 1)");  
				db_.exec("INSERT INTO categories VALUES (1, 'Pad', 'ffffffb3', 1)");
				db_.exec("INSERT INTO categories VALUES (2, 'Brass', 'ffbebada', 1)");
				db_.exec("INSERT INTO categories VALUES (3, 'Organ', 'fffb8072', 1)");
				db_.exec("INSERT INTO categories VALUES (4, 'Keys', 'ff80b1d3', 1)");
				db_.exec("INSERT INTO categories VALUES (5, 'Bass', 'fffdb462', 1)");
				db_.exec("INSERT INTO categories VALUES (6, 'Arp', 'ffb3de69', 1)");
				db_.exec("INSERT INTO categories VALUES (7, 'Pluck', 'fffccde5', 1)");
				db_.exec("INSERT INTO categories VALUES (8, 'Drone', 'ffd9d9d9', 1)");
				db_.exec("INSERT INTO categories VALUES (9, 'Drum', 'ffbc80bd', 1)");
				db_.exec("INSERT INTO categories VALUES (10, 'Bell', 'ffccebc5', 1)");
				db_.exec("INSERT INTO categories VALUES (11, 'SFX', 'ffffed6f', 1)");
				db_.exec("INSERT INTO categories VALUES (12, 'Ambient', '', 1)");
				db_.exec("INSERT INTO categories VALUES (13, 'Wind', '', 1)");
				db_.exec("INSERT INTO categories VALUES (14, 'Voice', '', 1)");
				db_.exec("UPDATE schema_version SET number = 5");
				transaction.commit();
			}
		}

		void createSchema() {
			if (false)
			{
				SQLite::Transaction transaction(db_);
				db_.exec("DROP TABLE IF EXISTS patches");
				db_.exec("DROP TABLE IF EXISTS imports");
				db_.exec("DROP TABLE IF EXISTS schema_version");
				transaction.commit();
			}

			if (!db_.tableExists("patches") || !db_.tableExists("imports")) {
				SQLite::Transaction transaction(db_);

				db_.exec("CREATE TABLE IF NOT EXISTS patches (synth TEXT, md5 TEXT UNIQUE, name TEXT, type INTEGER, data BLOB, favorite INTEGER, hidden INTEGER, sourceID TEXT, sourceName TEXT,"
					" sourceInfo TEXT, midiProgramNo INTEGER, categories INTEGER, categoryUserDecision INTEGER)");
				db_.exec("CREATE TABLE IF NOT EXISTS imports (synth TEXT, name TEXT, id TEXT, date TEXT)");
				db_.exec("CREATE TABLE IF NOT EXISTS categories (bitIndex INTEGER, name TEXT, color TEXT, active INTEGER)");
				db_.exec("CREATE TABLE IF NOT EXISTS schema_version (number INTEGER)");

				// Commit transaction
				transaction.commit();
			}

			// Check if schema needs to be migrated
			SQLite::Statement schemaQuery(db_, "SELECT number FROM schema_version");
			if (schemaQuery.executeStep()) {
				int version = schemaQuery.getColumn("number").getInt();
				if (version < SCHEMA_VERSION) {
					migrateSchema(version);
				}
			}
			else {
				// Ups, completely empty database, need to insert current schema version
				int rows = db_.exec("INSERT INTO schema_version VALUES (" + String(SCHEMA_VERSION).toStdString() + ")");
				if (rows != 1) {
					jassert(false);
					AlertWindow::showMessageBox(AlertWindow::WarningIcon, "SQL Error", "For whatever reason couldn't insert the schema version number. Something is terribly wrong.");
				}
			}
		}

		bool putPatch(Synth *activeSynth, PatchHolder const &patch, std::string const &sourceID) {
			try {
				SQLite::Statement sql(db_, "INSERT INTO patches (synth, md5, name, type, data, favorite, hidden, sourceID, sourceName, sourceInfo, midiProgramNo, categories, categoryUserDecision)"
					" VALUES (:SYN, :MD5, :NAM, :TYP, :DAT, :FAV, :HID, :SID, :SNM, :SRC, :PRG, :CAT, :CUD)");

				// Insert values into prepared statement
				sql.bind(":SYN", activeSynth->getName().c_str());
				sql.bind(":MD5", patch.md5());
				sql.bind(":NAM", patch.patch()->patchName());
				sql.bind(":TYP", patch.getType());
				sql.bind(":DAT", patch.patch()->data().data(), (int) patch.patch()->data().size());
				sql.bind(":FAV", (int)patch.howFavorite().is());
				sql.bind(":HID", patch.isHidden());
				sql.bind(":SID", sourceID);
				sql.bind(":SNM", patch.sourceInfo()->toDisplayString(activeSynth));
				sql.bind(":SRC", patch.sourceInfo()->toString());
				auto realPatch = std::dynamic_pointer_cast<Patch>(patch.patch());
				if (realPatch) {
					sql.bind(":PRG", realPatch->patchNumber()->midiProgramNumber().toZeroBased());
				}
				else {
					sql.bind(":PRG", 0);
				}
				sql.bind(":CAT", patch.categoriesAsBitfield());
				sql.bind(":CUD", patch.userDecisionAsBitfield());
				
				sql.exec();
			}
			catch (SQLite::Exception &ex) {
				AlertWindow::showMessageBox(AlertWindow::WarningIcon, "SQL Exception", ex.what());
			}
			return true;
		}

		std::vector<ImportInfo> getImportsList(Synth *activeSynth) {
			SQLite::Statement query(db_, "SELECT imports.name, id, count(patches.md5) AS patchCount FROM imports JOIN patches on imports.id == patches.sourceID WHERE patches.synth = :SYN AND imports.synth = :SYN GROUP BY imports.id ORDER BY date");
			query.bind(":SYN", activeSynth->getName());
			std::vector<ImportInfo> result;
			while (query.executeStep()) {
				std::string description = (boost::format("%s (%d)") % query.getColumn("name").getText() % query.getColumn("patchCount").getInt()).str();
				result.push_back({ query.getColumn("name").getText(), description, query.getColumn("id").getText() });
			}
			return result;
		}

		std::string buildWhereClause(PatchFilter filter) {
			std::string where = " WHERE synth = :SYN";
			if (!filter.importID.empty()) {
				where += " AND sourceID = :SID";
			}
			if (!filter.name.empty()) {
				where += " AND name LIKE :NAM";
			}
			if (filter.onlyFaves) {
				where += " AND favorite == 1";
			}
			if (filter.onlySpecifcType) {
				where += " AND type == :TYP";
			}
			if (!filter.showHidden) {
				where += " AND (hidden is null or hidden != 1)";
			}
			if (filter.onlyUntagged) {
				where += " AND categories == 0";
			}
			else if (!filter.categories.empty()) {
				// Empty category filter set will of course return everything
				//TODO this has bad query performance as it will force a table scan, but for now I cannot see this becoming a problem as long as the database is not multi-tenant
				// The correct way to do this would be to create a many to many relationship and run an "exists" query or join/unique the category table. Returning the list of categories also requires 
				// a concat on sub-query, so we're running into more complex SQL territory here.
				where += " AND (categories & :CAT != 0)";
			}
			return where;
		}

		void bindWhereClause(SQLite::Statement &query, PatchFilter filter) {
			query.bind(":SYN", filter.activeSynth->getName());
			if (!filter.importID.empty()) {
				query.bind(":SID", filter.importID);
			}
			if (!filter.name.empty()) {
				query.bind(":NAM", "%" + filter.name + "%");
			}
			if (filter.onlySpecifcType) {
				query.bind(":TYP", filter.typeID);
			}
			if (!filter.categories.empty()) {
				query.bind(":CAT", Category::categorySetAsBitfield(filter.categories));
			}
		}

		int getPatchesCount(PatchFilter filter) {
			SQLite::Statement query(db_, "SELECT count(*) FROM patches" + buildWhereClause(filter));
			bindWhereClause(query, filter);
			if (query.executeStep()) {
				return query.getColumn(0).getInt();
			}
			return 0;
		}

		std::vector<Category> getCategories() {
			SQLite::Statement query(db_, "SELECT * FROM categories ORDER BY bitIndex");
			std::vector<Category> result;
			while (query.executeStep()) {
				auto bitIndex = query.getColumn("bitIndex").getInt() + 1;
				auto name = query.getColumn("name").getText();
				auto colorName = query.getColumn("color").getText();
				result.emplace_back(name, Colour::fromString(colorName), bitIndex);
			}
			return result;
		}

		bool getPatches(PatchFilter filter, std::vector<PatchHolder> &result, int skip, int limit) {
			// We need the current categories
			auto categories = getCategories();

			SQLite::Statement query(db_, "SELECT * FROM patches " + buildWhereClause(filter) + " ORDER BY sourceID, midiProgramNo LIMIT :LIM OFFSET :OFS");
			bindWhereClause(query, filter);
			query.bind(":LIM", limit);
			query.bind(":OFS", skip);
			while (query.executeStep()) {
				std::shared_ptr<DataFile> newPatch;

				// Create the patch itself, from the BLOB stored
				auto dataColumn = query.getColumn("data");
				if (dataColumn.isBlob()) {
					std::vector<uint8> patchData((uint8 *)dataColumn.getBlob(), ((uint8 *)dataColumn.getBlob()) + dataColumn.getBytes());
					newPatch = filter.activeSynth->patchFromPatchData(patchData, "unknown", MidiProgramNumber::fromZeroBase(0));
				}

				if (newPatch) {
					auto sourceColumn = query.getColumn("sourceInfo");
					if (sourceColumn.isText()) {
						PatchHolder holder(filter.activeSynth, SourceInfo::fromString(sourceColumn.getString()), newPatch);
						auto favoriteColumn = query.getColumn("favorite");
						if (favoriteColumn.isInteger()) {
							holder.setFavorite(Favorite(favoriteColumn.getInt()));
						}
						/*auto typeColumn = query.getColumn("type");
						if (typeColumn.isInteger()) {
							holder.setType(typeColumn.getInt());
						}*/
						auto hiddenColumn = query.getColumn("hidden");
						if (hiddenColumn.isInteger()) {
							holder.setHidden(hiddenColumn.getInt() == 1);
						}
						holder.setCategoriesFromBitfield(categories, query.getColumn("categories").getInt64());
						holder.setUserDecisionsFromBitfield(categories, query.getColumn("categoryUserDecision").getInt64());
						result.push_back(holder);
					}
					else {
						jassert(false);
					}
					
				}
				else {
					jassert(false);
				}
			}
			return true;
		}

		std::map<std::string, PatchHolder> bulkGetPatches(Synth *activeSynth, std::vector<PatchHolder> & patches, ProgressHandler *progress) {
			// Query the database for exactly those patches, we want to know which ones are already there!
			std::map<std::string, PatchHolder> result;

			int checkedForExistance = 0;
			for (auto ph : patches) {
				if (progress && progress->shouldAbort()) return std::map<std::string, PatchHolder>();
				// First, calculate list of "IDs"
				// Query the database if this exists. Normally, I would bulk, but as the database is local for now I think we're going to be fine
				try {
					std::string md5 = ph.md5();
					SQLite::Statement query(db_, "SELECT md5 FROM patches WHERE md5 = :MD5");
					query.bind(":MD5", md5);
					if (query.executeStep()) {
						result.emplace(md5, ph);
					}
				} 
				catch (SQLite::Exception &ex) {
					AlertWindow::showMessageBox(AlertWindow::WarningIcon, "SQL Exception", ex.what());
				}
				if (progress) progress->setProgressPercentage(checkedForExistance++ / (double)patches.size());
			}
			return result;
		}

		void updatePatch(Synth *activeSynth, PatchHolder newPatch, PatchHolder existingPatch) {
			// For now, only run an update query if the newPatch Favorite is different from the database Favorite and is not "don't know"
			if (newPatch.howFavorite().is() != Favorite::TFavorite::DONTKNOW) {
				SQLite::Statement sql(db_, "UPDATE patches SET favorite = :FAV WHERE md5 = :MD5");
				sql.bind(":FAV", (int) newPatch.howFavorite().is());
				sql.bind(":MD5", newPatch.md5());
				if (sql.exec() != 1) {
					//TODO - this would happen e.g. if the database is locked, because somebody is trying to modify it with DB browser (me for instance)
					jassert(false);
					throw new std::runtime_error("FATAL, I don't want to ruin your database");
				}
			}

			// Also, update the categories bit vector and the hidden field
			{
				SQLite::Statement sql(db_, "UPDATE patches SET categories = :CAT, categoryUserDecision = :CUD, hidden = :HID, name = :NAM, data = :DAT WHERE md5 = :MD5");
				sql.bind(":CAT", newPatch.categoriesAsBitfield());
				sql.bind(":CUD", newPatch.userDecisionAsBitfield());
				sql.bind(":NAM", newPatch.patch()->patchName());
				sql.bind(":DAT", newPatch.patch()->data().data(), (int)newPatch.patch()->data().size());
				sql.bind(":HID", newPatch.isHidden());
				sql.bind(":MD5", newPatch.md5());
				if (sql.exec() != 1) {
					jassert(false);
					throw new std::runtime_error("FATAL, I don't want to ruin your database");
				}
			}
		}

		size_t mergePatchesIntoDatabase(Synth *activeSynth, std::vector<PatchHolder> &patches, std::vector<PatchHolder> &outNewPatches, ProgressHandler *progress) {
			// This works by doing a bulk get operation for the patches from the database...
			auto knownPatches = bulkGetPatches(activeSynth, patches, progress);

			SQLite::Transaction transaction(db_);

			int loop = 0;
			for (auto &patch : patches) {
				if (progress && progress->shouldAbort()) return 0;
				if (knownPatches.find(patch.md5()) != knownPatches.end()) {
					// Update the database with the new info
					updatePatch(activeSynth, patch, knownPatches[patch.md5()]);
				}
				else {
					// This is a new patch - it needs to be uploaded into the database!
					outNewPatches.push_back(patch);
				}
				if (progress) progress->setProgressPercentage(loop++ / (double)patches.size());
			}

			// Check if all new patches are editBuffer patches (aka have an invalid MidiBank)
			std::string source_id = "EditBufferImport";
			std::string importName;
			for (auto newPatch : outNewPatches) {
				if (SourceInfo::isEditBufferImport(newPatch.sourceInfo())) {
					// EditBuffer, nothing to do
					// In case this is an EditBuffer import (no bank known), always use the same "fake UUID" "EditBufferImport"
					importName = "Edit buffer imports";
				}
				else {
					// Generate a UUID that will be used to bind all patches during this import together. 
					Uuid source_uuid;
					source_id = source_uuid.toString().toStdString();
					if (importName.empty()) {
						// Use the importName of the first patch. This is not ideal, but currently I have no better idea
						importName = newPatch.sourceInfo()->toDisplayString(activeSynth);
					}
				}
			}

			//TODO can be replaced by repaired bulkPut
			int uploaded = 0;
			std::map<String, PatchHolder> md5Inserted;
			for (auto newPatch : outNewPatches) {
				if (progress && progress->shouldAbort()) return uploaded;
				if (md5Inserted.find(newPatch.md5()) != md5Inserted.end()) {
					auto duplicate = md5Inserted[newPatch.md5()];
					SimpleLogger::instance()->postMessage("Skipping patch " + String(newPatch.patch()->patchName()) + " because it is a duplicate of " + duplicate.patch()->patchName());
				}
				else {
					putPatch(activeSynth, newPatch, source_id);
					md5Inserted[newPatch.md5()] = newPatch;
					uploaded++;
				}
				if (progress) progress->setProgressPercentage(uploaded / (double)outNewPatches.size());
			}

			if (uploaded > 0) {
				// Check if this import already exists (this should only happen with the EditBufferImport special case)
				bool alreadyExists = false;
				SQLite::Statement query(db_, "SELECT count(*) AS numExisting FROM imports WHERE synth = :SYN and id = :SID");
				query.bind(":SYN", activeSynth->getName());
				query.bind(":SID", source_id);
				if (query.executeStep()) {
					auto existing = query.getColumn("numExisting");
					if (existing.getInt() == 1) {
						alreadyExists = true;
					}
				}

				if (!alreadyExists) {
					// Record this import in the import table for later filtering! The name of the import might differ for different patches (bulk import), use the first patch to calculate it
					SQLite::Statement sql(db_, "INSERT INTO imports (synth, name, id, date) VALUES (:SYN, :NAM, :SID, datetime('now'))");
					sql.bind(":SYN", activeSynth->getName());
					sql.bind(":NAM", importName);
					sql.bind(":SID", source_id);
					sql.exec();
				}
			}

			transaction.commit();

			return uploaded;
		}

		std::shared_ptr<AutomaticCategorizer> getCategorizer() {
			// The Categorizer currently is constructed from two sources - the list of categories in the database including the bit index
			// The auto-detection rules are stored in the jsonc file.
			// This needs to be merged.
			auto categorizer = std::make_shared<AutomaticCategorizer>();

			// Load the categories from the database table
			auto databaseCategories = getCategories();

			// Load the json Definition
			AutomaticCategorizer rules;
			rules.loadFromString(autocategoryDefinitions_);
			auto max = std::max_element(databaseCategories.begin(), databaseCategories.end(), [](Category a, Category b) { return a.bitIndex < b.bitIndex; });
			int bitindex = 0;
			if (max != databaseCategories.end()) {
				bitindex = max->bitIndex;
			}

			// First pass - check that all categories referenced in the auto category file are stored in the database, else they will have no bit index!
			SQLite::Transaction transaction(db_);
			for (auto rule : rules.predefinedCategories()) {
				auto exists = false;
				for (auto cat : databaseCategories) {
					if (cat.category == rule.category().category) {
						exists = true;
						break;
					}
				}
				if (!exists) {
					// Need to create a new entry in the database
					if (bitindex < 63) {
						bitindex++;
						SQLite::Statement sql(db_, "INSERT INTO categories VALUES (:BIT, :NAM, :COL, 1)");
						sql.bind(":BIT", bitindex);
						sql.bind(":NAM", rule.category().category);
						sql.bind(":COL", rule.category().color.toDisplayString(true).toStdString());
						sql.exec();
					}
					else {
						jassert(false);
						SimpleLogger::instance()->postMessage("FATAL ERROR - Can only deal with 64 different categories. Please remove some categories from the rules file!");
						return categorizer;
					}
				}
			}
			transaction.commit();

			// Refresh from database
			databaseCategories = getCategories();

			// Now we need to merge the database persisted categories with the ones defined in the automatic categories from the json string
			bool exists = false;
			for (auto cat : databaseCategories) {
				for (auto rule : rules.predefinedCategories()) {
					if (cat.category == rule.category().category) {
						// Copy the rules
						exists = true;
						categorizer->addAutoCategory(AutoCategory(cat, rule.patchNameMatchers()));
						break;
					}
				}
				if (!exists) {
					// That just means there are no rules, but it needs to be added to the list of available categories anyway
					categorizer->addAutoCategory(AutoCategory(cat, std::vector<std::string>()));
				}
			}

			return categorizer;
		}

		void setAutocategorizationRules(std::string const &jsonDefinition) {
			//TODO - this needs to be persisted in the database as well
			autocategoryDefinitions_ = jsonDefinition;
		}

	private:
		SQLite::Database db_;
		std::string autocategoryDefinitions_;
	};

	PatchDatabase::PatchDatabase() {
		impl.reset(new PatchDataBaseImpl());
	}

	PatchDatabase::~PatchDatabase() {
	}

	int PatchDatabase::getPatchesCount(PatchFilter filter)
	{
		return impl->getPatchesCount(filter);
	}

	bool PatchDatabase::putPatch(Synth *activeSynth, PatchHolder const &patch) {
		// From the logic, this is an UPSERT (REST call put)
		// Use the merge functionality for this!
		std::vector<PatchHolder> newPatches;
		newPatches.push_back(patch);
		std::vector<PatchHolder> insertedPatches;
		return impl->mergePatchesIntoDatabase(activeSynth, newPatches, insertedPatches, nullptr);
	}

	bool PatchDatabase::putPatches(Synth *activeSynth, std::vector<PatchHolder> const &patches) {
		jassert(false);
		return false;
	}

	std::shared_ptr<AutomaticCategorizer> PatchDatabase::getCategorizer() 
	{
		return impl->getCategorizer();
	}

	void PatchDatabase::setAutocategorizationRules(std::string const &jsonDefinition)
	{
		impl->setAutocategorizationRules(jsonDefinition);
	}

	std::vector<PatchHolder> PatchDatabase::getPatches(PatchFilter filter, int skip, int limit)
	{
			std::vector<PatchHolder> result;
			bool success = impl->getPatches(filter, result, skip, limit);
			if (success) {
			return result;
			}
			else {
			return {};
			}
	}

	void PatchDatabase::getPatchesAsync(PatchFilter filter, std::function<void(std::vector<PatchHolder> const &)> finished, int skip, int limit)
	{
		pool_.addJob([this, filter, finished, skip, limit]() {
			auto result = getPatches(filter, skip, limit);
			MessageManager::callAsync([finished, result]() {
				finished(result);
			});
		});
	}

	size_t PatchDatabase::mergePatchesIntoDatabase(Synth *activeSynth, std::vector<PatchHolder> &patches, std::vector<PatchHolder> &outNewPatches, ProgressHandler *progress)
	{
		return impl->mergePatchesIntoDatabase(activeSynth, patches, outNewPatches, progress);
	}

	std::vector<ImportInfo> PatchDatabase::getImportsList(Synth *activeSynth) const {
		return impl->getImportsList(activeSynth);
	}

	std::vector<Category> PatchDatabase::getCategories() const {
		return impl->getCategories();
	}

}

