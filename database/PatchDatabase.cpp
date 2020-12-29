/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchDatabase.h"

#include "Capability.h"
#include "Patch.h"
#include "Logger.h"
#include "PatchHolder.h"
#include "StoredPatchNameCapability.h"

#include "JsonSchema.h"
#include "JsonSerialization.h"

#include "ProgressHandler.h"

#include "FileHelpers.h"

#include <iostream>
#include <boost/format.hpp>

#include "SQLiteCpp/Database.h"
#include "SQLiteCpp/Statement.h"
#include "SQLiteCpp/Transaction.h"

namespace midikraft {

	const std::string kDataBaseFileName = "SysexDatabaseOfAllPatches.db3";
	const std::string kDataBaseBackupSuffix = "-backup";

	const int SCHEMA_VERSION = 5;
	/* History */
	/* 1 - Initial schema */
	/* 2 - adding hidden flag (aka deleted) */
	/* 3 - adding type integer to patch (to differentiate voice, patch, layer, tuning...) */
	/* 4 - forgot to migrate existing data NULL to 0 */
	/* 5 - adding bank number column for better sorting of multi-imports */

	class PatchDatabase::PatchDataBaseImpl {
	public:
		PatchDataBaseImpl(std::string const &databaseFile) : db_(databaseFile.c_str(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE) {
			createSchema();
			manageBackupDiskspace(kDataBaseBackupSuffix);
		}

		~PatchDataBaseImpl() {
			PatchDataBaseImpl::makeDatabaseBackup(kDataBaseBackupSuffix);
		}

		std::string makeDatabaseBackup(String const &suffix) {
			File dbFile(db_.getFilename());
			if (dbFile.existsAsFile()) {
				File backupCopy(dbFile.getParentDirectory().getNonexistentChildFile(dbFile.getFileNameWithoutExtension() + suffix, dbFile.getFileExtension(), false));
				db_.backup(backupCopy.getFullPathName().toStdString().c_str(), SQLite::Database::Save);
				return backupCopy.getFullPathName().toStdString();
			}
			else {
				jassertfalse;
				return "";
			}
		}

		void makeDatabaseBackup(File databaseFileToCreate) {
			if (databaseFileToCreate.existsAsFile()) {
				// The dialog surely has asked that we allow that
				databaseFileToCreate.deleteFile();
			}
			db_.backup(databaseFileToCreate.getFullPathName().toStdString().c_str(), SQLite::Database::Save);
		}

		void backupIfNecessary(bool &done) {
			if (!done) {
				makeDatabaseBackup("-before-migration");
				done = true;
			}
		}

		//TODO a better strategy than the last 3 backups would be to group by week, month, to keep older ones
		void manageBackupDiskspace(String suffix) {
			// Build a list of all backups on disk and calculate the size of it. Do not keep more than 500 mio bytes or the last 3 copies if they together make up more than 500 mio bytes
			File activeDBFile(db_.getFilename());
			File backupDirectory(activeDBFile.getParentDirectory());
			auto backupsFiles = backupDirectory.findChildFiles(File::TypesOfFileToFind::findFiles, false, activeDBFile.getFileNameWithoutExtension() + suffix + "*" + activeDBFile.getFileExtension());
			size_t backupSize = 0;
			size_t keptBackupSize = 0;
			size_t numKept = 0;
			// Sort by date ascending
			auto sortComperator = FileDateComparatorNewestFirst(); // gcc wants this as an l-value
			backupsFiles.sort(sortComperator, false);
			for (auto file : backupsFiles) {
				backupSize += file.getSize();
				if (backupSize > 500000000 && numKept > 2) {
					//SimpleLogger::instance()->postMessage("Removing database backup file to keep disk space used below 50 million bytes: " + file.getFullPathName());
					if (!file.deleteFile()) {
						SimpleLogger::instance()->postMessage("Error - failed to remove extra backup file, please check file permissions: " + file.getFullPathName());
					}
				}
				else {
					numKept++;
					keptBackupSize += file.getSize();
				}
			}
			if (backupSize != keptBackupSize) {
				SimpleLogger::instance()->postMessage((boost::format("Removing all but %d backup files reducing disk space used from %d to %d bytes") % numKept % backupSize % keptBackupSize).str());
			}
		}

		void migrateSchema(int currentVersion) {
			bool hasBackuped = false;

			if (currentVersion < 2) {
				backupIfNecessary(hasBackuped);
				SQLite::Transaction transaction(db_);
				db_.exec("ALTER TABLE patches ADD COLUMN hidden INTEGER");
				db_.exec("UPDATE schema_version SET number = 2");
				transaction.commit();
			}
			if (currentVersion < 3) {
				backupIfNecessary(hasBackuped);
				SQLite::Transaction transaction(db_);
				db_.exec("ALTER TABLE patches ADD COLUMN type INTEGER");
				db_.exec("UPDATE schema_version SET number = 3");
				transaction.commit();
			}
			if (currentVersion < 4) {
				backupIfNecessary(hasBackuped);
				SQLite::Transaction transaction(db_);
				db_.exec("UPDATE patches SET type = 0 WHERE type is NULL");
				db_.exec("UPDATE schema_version SET number = 4");
				transaction.commit();
			}
			if (currentVersion < 5) {
				backupIfNecessary(hasBackuped);
				SQLite::Transaction transaction(db_);
				db_.exec("ALTER TABLE patches ADD COLUMN midiBankNo INTEGER");
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
					" sourceInfo TEXT, midiBankNo INTEGER, midiProgramNo INTEGER, categories INTEGER, categoryUserDecision INTEGER)");
				db_.exec("CREATE TABLE IF NOT EXISTS imports (synth TEXT, name TEXT, id TEXT, date TEXT)");
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
				else if (version > SCHEMA_VERSION) {
					// This is a database from the future, can't open!
					std::string message = (boost::format("Cannot open database file %s - this was produced with a newer version of KnobKraft Orm, schema version is %d.") % db_.getFilename() % version).str();
					AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Database Error", message);
					throw new SQLite::Exception(message);
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

		bool putPatch(PatchHolder const &patch, std::string const &sourceID) {
			try {
				SQLite::Statement sql(db_, "INSERT INTO patches (synth, md5, name, type, data, favorite, hidden, sourceID, sourceName, sourceInfo, midiBankNo, midiProgramNo, categories, categoryUserDecision)"
					" VALUES (:SYN, :MD5, :NAM, :TYP, :DAT, :FAV, :HID, :SID, :SNM, :SRC, :BNK, :PRG, :CAT, :CUD)");

				// Insert values into prepared statement
				sql.bind(":SYN", patch.synth()->getName().c_str());
				sql.bind(":MD5", patch.md5());
				sql.bind(":NAM", patch.name());
				sql.bind(":TYP", patch.getType());
				sql.bind(":DAT", patch.patch()->data().data(), (int)patch.patch()->data().size());
				sql.bind(":FAV", (int)patch.howFavorite().is());
				sql.bind(":HID", patch.isHidden());
				sql.bind(":SID", sourceID);
				sql.bind(":SNM", patch.sourceInfo()->toDisplayString(patch.synth(), false));
				sql.bind(":SRC", patch.sourceInfo()->toString());
				sql.bind(":BNK", patch.bankNumber().toZeroBased());
				sql.bind(":PRG", patch.patchNumber().toZeroBased());
				sql.bind(":CAT", patch.categoriesAsBitfield());
				sql.bind(":CUD", patch.userDecisionAsBitfield());

				sql.exec();
			}
			catch (SQLite::Exception &ex) {
				SimpleLogger::instance()->postMessage((boost::format("DATABASE ERROR: SQL Exception %s") % ex.what()).str());
				//AlertWindow::showMessageBox(AlertWindow::WarningIcon, "SQL Exception", ex.what());
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
			std::string where = " WHERE 1 == 1 ";
			if (!filter.synths.empty()) {
				//TODO does Sqlite do "IN" clause?
				where += " AND ( ";
				int s = 0;
				for (auto synth : filter.synths) {
					if (s != 0) where += " OR ";
					where += "synth = " + synthVariable(s++);
				}
				where += " ) ";
			}
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

		std::string synthVariable(int no) {
			// Calculate a variable name to bind the synth name to. This will blow up if you query for more than 99 synths.
			return (boost::format(":S%02d") % no).str();
		}

		void bindWhereClause(SQLite::Statement &query, PatchFilter filter) {
			int s = 0;
			for (auto const &synth : filter.synths) {
				query.bind(synthVariable(s++), synth.second.lock()->getName());
			}
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
				query.bind(":CAT", PatchHolder::categorySetAsBitfield(filter.categories));
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

		bool loadPatchFromQueryRow(std::shared_ptr<Synth> synth, SQLite::Statement &query, std::vector<PatchHolder> &result) {
			std::shared_ptr<DataFile> newPatch;

			// Create the patch itself, from the BLOB stored
			auto dataColumn = query.getColumn("data");
			
			if (dataColumn.isBlob()) {
				std::vector<uint8> patchData((uint8 *)dataColumn.getBlob(), ((uint8 *)dataColumn.getBlob()) + dataColumn.getBytes());
				//TODO I should not need the midiProgramNumber here
				int midiProgramNumber = query.getColumn("midiProgramNo").getInt();
				newPatch = synth->patchFromPatchData(patchData, MidiProgramNumber::fromZeroBase(midiProgramNumber));
			}

			if (newPatch) {
				auto sourceColumn = query.getColumn("sourceInfo");
				if (sourceColumn.isText()) {
					int midiBankNumber = query.getColumn("midiBankNo").getInt();
					int midiProgramNumber = query.getColumn("midiProgramNo").getInt();
					PatchHolder holder(synth, SourceInfo::fromString(sourceColumn.getString()), newPatch, MidiBankNumber::fromZeroBase(midiBankNumber), MidiProgramNumber::fromZeroBase(midiProgramNumber));

					std::string patchName = query.getColumn("name").getString();
					holder.setName(patchName);
					std::string sourceId = query.getColumn("sourceID");
					holder.setSourceId(sourceId);

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
					holder.setCategoriesFromBitfield(query.getColumn("categories").getInt64());
					holder.setUserDecisionsFromBitfield(query.getColumn("categoryUserDecision").getInt64());
					
					result.push_back(holder);
					return true;
				}
				else {
					jassert(false);
				}
			}
			else {
				jassert(false);
			}
			return false;
		}

		bool getSinglePatch(std::shared_ptr<Synth> synth, std::string const &md5, std::vector<PatchHolder> &result) {
			SQLite::Statement query(db_, "SELECT * FROM patches WHERE md5 = :MD5 and synth = :SYN");
			query.bind(":SYN", synth->getName());
			query.bind(":MD5", md5);
			if (query.executeStep()) {
				return loadPatchFromQueryRow(synth, query, result);
			}
			return false;
		}

		bool getPatches(PatchFilter filter, std::vector<PatchHolder> &result, std::vector<std::pair<std::string, PatchHolder>> &needsReindexing, int skip, int limit) {
			std::string selectStatement = "SELECT * FROM patches " + buildWhereClause(filter) + " ORDER BY sourceID, midiBankNo, midiProgramNo ";
			if (limit != -1) {
				selectStatement += " LIMIT :LIM ";
				selectStatement += " OFFSET :OFS";
			}
			SQLite::Statement query(db_, selectStatement.c_str());

			bindWhereClause(query, filter);
			if (limit != -1) {
				query.bind(":LIM", limit);
				query.bind(":OFS", skip);
			}
			while (query.executeStep()) {
				// Find the synth this patch is for
				auto synthName = query.getColumn("synth");
				if (filter.synths.find(synthName) == filter.synths.end()) {
					SimpleLogger::instance()->postMessage((boost::format("Program error, query returned patch for synth %s which was not part of the filter") % synthName).str());
					continue;
				}
				auto thisSynth = filter.synths[synthName].lock();

				if (loadPatchFromQueryRow(thisSynth, query, result)) {
					// Check if the MD5 is the correct one (the algorithm might have changed!)
					std::string md5stored = query.getColumn("md5");
					if (result.back().md5() != md5stored) {
						needsReindexing.emplace_back(md5stored, result.back());
					}
				}
			}
			return true;
		}

		std::map<std::string, PatchHolder> bulkGetPatches(std::vector<PatchHolder> const& patches, ProgressHandler *progress) {
			// Query the database for exactly those patches, we want to know which ones are already there!
			std::map<std::string, PatchHolder> result;

			int checkedForExistance = 0;
			for (auto ph : patches) {
				if (progress && progress->shouldAbort()) return std::map<std::string, PatchHolder>();
				// First, calculate list of "IDs"
				// Query the database if this exists. Normally, I would bulk, but as the database is local for now I think we're going to be fine
				try {
					std::string md5 = ph.md5();
					SQLite::Statement query(db_, "SELECT md5, name, midiProgramNo, midiBankNo FROM patches WHERE md5 = :MD5 and synth = :SYN");
					query.bind(":SYN", ph.synth()->getName());
					query.bind(":MD5", md5);
					if (query.executeStep()) {
						int midiBankNumber = query.getColumn("midiBankNo").getInt();
						int midiProgramNumber = query.getColumn("midiProgramNo").getInt();
						PatchHolder existingPatch(ph.smartSynth(), ph.sourceInfo(), nullptr, MidiBankNumber::fromZeroBase(midiBankNumber), MidiProgramNumber::fromZeroBase(midiProgramNumber));
						std::string name = query.getColumn("name");
						existingPatch.setName(name);
						result.emplace(md5, existingPatch);
					}
				}
				catch (SQLite::Exception &ex) {
					SimpleLogger::instance()->postMessage((boost::format("DATABASE ERROR: SQL Exception %s") % ex.what()).str());
				}
				if (progress) progress->setProgressPercentage(checkedForExistance++ / (double)patches.size());
			}
			return result;
		}

		std::string prependWithComma(std::string const &target, std::string const &suffix) {
			if (target.empty())
				return suffix;
			return target + ", " + suffix;
		}

		void calculateMergedCategories(PatchHolder &newPatch, PatchHolder existingPatch) {
			// Now this is fun - we are adding information of a new Patch to an existing Patch. We will try to respect the user decision,
			// but as we in the reindexing case do not know whether the new or the existing has "better" information, we will just merge the existing categories and 
			// user decisions. Adding a category most often is more useful than removing one

			// Turn off existing user decisions where a new user decision exists
			int64 newPatchUserDecided = newPatch.categoriesAsBitfield() & newPatch.userDecisionAsBitfield();
			int64 newPatchAutomatic = newPatch.categoriesAsBitfield() & ~newPatch.userDecisionAsBitfield();
			int64 oldUserDecided = existingPatch.categoriesAsBitfield() & existingPatch.userDecisionAsBitfield();

			// The new categories are calculated as all categories from the new patch, unless there is a user decision at the existing patch not marked as overridden by a new user decision
			// plus all existing patch categories where there is no new user decision
			newPatch.setCategoriesFromBitfield(newPatchUserDecided | (newPatchAutomatic & ~existingPatch.userDecisionAsBitfield()) | (oldUserDecided & ~newPatch.userDecisionAsBitfield()));

			// User decisions are now a union of both
			newPatch.setUserDecisionsFromBitfield(newPatch.userDecisionAsBitfield() | existingPatch.userDecisionAsBitfield());
		}

		int calculateMergedFavorite(PatchHolder const &newPatch, PatchHolder const &existingPatch) {
			if (newPatch.howFavorite().is() == Favorite::TFavorite::DONTKNOW) {
				// Keep the old value
				return (int)existingPatch.howFavorite().is();
			}
			else {
				// Use the new one
				return (int)newPatch.howFavorite().is();
			}
		}

		void updatePatch(PatchHolder newPatch, PatchHolder existingPatch, unsigned updateChoices) {
			if (updateChoices) {
				std::string updateClause;
				if (updateChoices & UPDATE_CATEGORIES) updateClause = prependWithComma(updateClause, "categories = :CAT, categoryUserDecision = :CUD");
				if (updateChoices & UPDATE_NAME) updateClause = prependWithComma(updateClause, "name = :NAM");
				if (updateChoices & UPDATE_HIDDEN) updateClause = prependWithComma(updateClause, "hidden = :HID");
				if (updateChoices & UPDATE_DATA) updateClause = prependWithComma(updateClause, "data = :DAT");
				if (updateChoices & UPDATE_FAVORITE) updateClause = prependWithComma(updateClause, "favorite = :FAV");

				SQLite::Statement sql(db_, "UPDATE patches SET " + updateClause + " WHERE md5 = :MD5 and synth = :SYN");
				if (updateChoices & UPDATE_CATEGORIES) {
					calculateMergedCategories(newPatch, existingPatch);
					sql.bind(":CAT", newPatch.categoriesAsBitfield());
					sql.bind(":CUD", newPatch.userDecisionAsBitfield());
				}
				if (updateChoices & UPDATE_NAME) {
					sql.bind(":NAM", newPatch.name());
				}
				if (updateChoices & UPDATE_DATA) {
					sql.bind(":DAT", newPatch.patch()->data().data(), (int)newPatch.patch()->data().size());
				}
				if (updateChoices & UPDATE_HIDDEN) {
					sql.bind(":HID", newPatch.isHidden());
				}
				if (updateChoices & UPDATE_FAVORITE) {
					sql.bind(":FAV", calculateMergedFavorite(newPatch, existingPatch));
				}
				sql.bind(":MD5", newPatch.md5());
				sql.bind(":SYN", existingPatch.synth()->getName());
				if (sql.exec() != 1) {
					jassert(false);
					throw new std::runtime_error("FATAL, I don't want to ruin your database");
				}
			}
		}

		bool hasDefaultName(DataFile *patch) {
			auto storedPatchNameCapa = midikraft::Capability::hasCapability<StoredPatchNameCapability>(patch);
			if (storedPatchNameCapa) {
				return storedPatchNameCapa->isDefaultName();
			}
			return false;
		}

		bool insertImportInfo(std::string const &synthname, std::string const &source_id, std::string const &importName) {
			// Check if this import already exists 
			SQLite::Statement query(db_, "SELECT count(*) AS numExisting FROM imports WHERE synth = :SYN and id = :SID");
			query.bind(":SYN", synthname);
			query.bind(":SID", source_id);
			if (query.executeStep()) {
				auto existing = query.getColumn("numExisting");
				if (existing.getInt() == 1) {
					return false;
				}
			}

			// Record this import in the import table for later filtering! The name of the import might differ for different patches (bulk import), use the first patch to calculate it
			SQLite::Statement sql(db_, "INSERT INTO imports (synth, name, id, date) VALUES (:SYN, :NAM, :SID, datetime('now'))");
			sql.bind(":SYN", synthname);
			sql.bind(":NAM", importName);
			sql.bind(":SID", source_id);
			sql.exec();
			return true;
		}

		size_t mergePatchesIntoDatabase(std::vector<PatchHolder> &patches, std::vector<PatchHolder> &outNewPatches, ProgressHandler *progress, unsigned updateChoice, bool useTransaction) {
			// This works by doing a bulk get operation for the patches from the database...
			auto knownPatches = bulkGetPatches(patches, progress);

			std::unique_ptr<SQLite::Transaction> transaction;
			if (useTransaction) {
				transaction = std::make_unique<SQLite::Transaction>(db_);
			}

			int loop = 0;
			int updatedNames = 0;
			for (auto &patch : patches) {
				if (progress && progress->shouldAbort()) return 0;

				auto md5_key = patch.md5();

				if (knownPatches.find(md5_key) != knownPatches.end()) {
					// Super special logic - do not set the name if the patch name is a default name to prevent us from losing manually given names or those imported from "better" sysex files
					unsigned onlyUpdateThis = updateChoice;
					if (hasDefaultName(patch.patch().get())) {
						onlyUpdateThis = onlyUpdateThis & (~UPDATE_NAME);
					}
					if ((onlyUpdateThis & UPDATE_NAME) && (patch.name() != knownPatches[md5_key].name())) {
						updatedNames++;
					}

					// Update the database with the new info. If more than the name should be updated, we first need to load the full existing patch (the bulkGetPatches only is a projection with the name loaded only)
					if (onlyUpdateThis != UPDATE_NAME) {
						std::vector<PatchHolder> result;
						if (getSinglePatch(patch.smartSynth(), md5_key, result)) {
							updatePatch(patch, result.back(), onlyUpdateThis);
						}
						else {
							jassertfalse;
						}
					}
					else {
						// We don't need to get back to the database if we only update the name
						updatePatch(patch, knownPatches[md5_key], UPDATE_NAME);
					}
				}
				else {
					// This is a new patch - it needs to be uploaded into the database!
					outNewPatches.push_back(patch);
				}
				if (progress) progress->setProgressPercentage(loop++ / (double)patches.size());
			}

			// Did we find better names? Then log it
			if (updatedNames > 0) {
				SimpleLogger::instance()->postMessage((boost::format("Updated %d patches in the database with new names") % updatedNames).str());
			}

			// Check if all new patches are editBuffer patches (aka have an invalid MidiBank)
			std::map<std::string, std::string> mapMD5_to_idOfImport;
			std::set<std::tuple<std::string, std::string, std::string>> importsToBeCreated;
			for (const auto& newPatch : outNewPatches) {
				if (!newPatch.sourceInfo()) {
					// Patch with no source info, probably very old or from 3rd party system
				} else if (SourceInfo::isEditBufferImport(newPatch.sourceInfo())) {
					// EditBuffer, nothing to do
					// In case this is an EditBuffer import (no bank known), always use the same "fake UUID" "EditBufferImport"
					mapMD5_to_idOfImport[newPatch.md5()] = "EditBufferImport";
					importsToBeCreated.emplace(newPatch.synth()->getName(), "EditBufferImport", "Edit buffer imports");
				}
				else {
					std::string importDisplayString = newPatch.sourceInfo()->toDisplayString(newPatch.synth(), true);;
					std::string importUID = newPatch.sourceInfo()->md5(newPatch.synth());
					mapMD5_to_idOfImport[newPatch.md5()] = importUID;
					importsToBeCreated.emplace(newPatch.synth()->getName(), importUID, importDisplayString);
				}
			}

			//TODO can be replaced by repaired bulkPut
			std::map<String, PatchHolder> md5Inserted;
			std::map<Synth *, int> synthsWithUploadedItems;
			int sumOfAll = 0;
			for (const auto& newPatch : outNewPatches) {
				if (progress && progress->shouldAbort()) {
					return (size_t) sumOfAll;
				}
				std::string patchMD5 = newPatch.md5();
				if (md5Inserted.find(patchMD5) != md5Inserted.end()) {
					auto duplicate = md5Inserted[patchMD5];

					// The new one could have better name?
					if (hasDefaultName(duplicate.patch().get()) && !hasDefaultName(newPatch.patch().get())) {
						updatePatch(newPatch, duplicate, UPDATE_NAME);
						SimpleLogger::instance()->postMessage("Updating patch name " + String(duplicate.name()) + " to better one: " + newPatch.name());
					}
					else {
						SimpleLogger::instance()->postMessage("Skipping patch " + String(newPatch.name()) + " because it is a duplicate of " + duplicate.name());
					}
				}
				else {
					if (newPatch.sourceId().empty()) {
						putPatch(newPatch, mapMD5_to_idOfImport[patchMD5]);
						if (synthsWithUploadedItems.find(newPatch.synth()) == synthsWithUploadedItems.end()) {
							// First time this synth sees an upload
							synthsWithUploadedItems[newPatch.synth()] = 0;
						}
						synthsWithUploadedItems[newPatch.synth()] += 1;
					}
					else {
						putPatch(newPatch, newPatch.sourceId());
					}
					md5Inserted[patchMD5] = newPatch;
					sumOfAll++;
				}
				if (progress) progress->setProgressPercentage(sumOfAll / (double)outNewPatches.size());
			}

			for (auto import : importsToBeCreated) {
				insertImportInfo(std::get<0>(import), std::get<1>(import), std::get<2>(import));
			}

			if (transaction) transaction->commit();

			return sumOfAll;
		}

		int deletePatches(PatchFilter filter) {
			// Build a delete query
			std::string deleteStatement = "DELETE FROM patches " + buildWhereClause(filter);
			SQLite::Statement query(db_, deleteStatement.c_str());
			bindWhereClause(query, filter);

			// Execute
			int rowsDeleted = query.exec();
			return rowsDeleted;
		}

		int deletePatches(std::string const &synth, std::vector<std::string> const &md5s) {
			int rowsDeleted = 0;
			for (auto md5 : md5s) {
				// Build a delete query
				std::string deleteStatement = "DELETE FROM patches WHERE md5 = :MD5 AND synth = :SYN";
				SQLite::Statement query(db_, deleteStatement.c_str());
				query.bind(":SYN", synth);
				query.bind(":MD5", md5);
				// Execute
				rowsDeleted += query.exec();
			}
			return rowsDeleted;
		}

		int reindexPatches(PatchFilter filter) {
			// Give up if more than one synth is selected
			if (filter.synths.size() > 1) {
				SimpleLogger::instance()->postMessage("Aborting reindexing - please select only one synth at a time in the advanced filter dialog!");
				return -1;
			}

			// Retrieve the patches to reindex
			std::vector<PatchHolder> result;
			std::vector<std::pair<std::string, PatchHolder>> toBeReindexed;
			if (getPatches(filter, result, toBeReindexed, 0, -1)) {
				if (!toBeReindexed.empty()) {
					std::vector<std::string> toBeDeleted;
					std::vector<PatchHolder> toBeReinserted;
					for (auto d : toBeReindexed) {
						toBeDeleted.push_back(d.first);
						toBeReinserted.push_back(d.second);
					}

					// This is a complex database operation, use a transaction to make sure we get all or nothing
					SQLite::Transaction transaction(db_);

					// We got everything into the RAM - do we dare do delete them from the database now?
					int deleted = deletePatches(filter.synths.begin()->second.lock()->getName(), toBeDeleted);
					if (deleted != (int) toBeReindexed.size()) {
						SimpleLogger::instance()->postMessage("Aborting reindexing - count of deleted patches does not match count of retrieved patches. Program Error.");
						return -1;
					}

					// Now insert the retrieved patches back into the database. The merge logic will handle the multiple instance situation
					std::vector<PatchHolder> remainingPatches;
					mergePatchesIntoDatabase(toBeReinserted, remainingPatches, nullptr, UPDATE_ALL, false);
					transaction.commit();

					return getPatchesCount(filter);
				}
				else {
					SimpleLogger::instance()->postMessage("None of the selected patches needed reindexing skipping!");
					return getPatchesCount(filter);
				}
			}
			else {
				SimpleLogger::instance()->postMessage("Aborting reindexing - database error retrieving the filtered patches");
				return -1;
			}

		}

		std::string databaseFileName() const
		{
			return db_.getFilename();
		}


	private:
		SQLite::Database db_;
	};

	PatchDatabase::PatchDatabase() {
		impl.reset(new PatchDataBaseImpl(generateDefaultDatabaseLocation()));
	}

	PatchDatabase::PatchDatabase(std::string const &databaseFile) {
		impl.reset(new PatchDataBaseImpl(databaseFile));
	}

	PatchDatabase::~PatchDatabase() {
	}

	std::string PatchDatabase::getCurrentDatabaseFileName() const
	{
		return impl->databaseFileName();
	}

	bool PatchDatabase::switchDatabaseFile(std::string const &newDatabaseFile)
	{
		try {
			auto newDatabase = new PatchDataBaseImpl(newDatabaseFile);
			// If no exception was thrown, this worked
			impl.reset(newDatabase);
			return true;
		}
		catch (SQLite::Exception &ex) {
			SimpleLogger::instance()->postMessage("Failed to open database: " + String(ex.what()));
		}
		return false;
	}

	int PatchDatabase::getPatchesCount(PatchFilter filter)
	{
		return impl->getPatchesCount(filter);
	}

	bool PatchDatabase::putPatch(PatchHolder const &patch) {
		// From the logic, this is an UPSERT (REST call put)
		// Use the merge functionality for this!
		std::vector<PatchHolder> newPatches;
		newPatches.push_back(patch);
		std::vector<PatchHolder> insertedPatches;
		return impl->mergePatchesIntoDatabase(newPatches, insertedPatches, nullptr, UPDATE_ALL, true);
	}

	bool PatchDatabase::putPatches(std::vector<PatchHolder> const &patches) {
		ignoreUnused(patches);
		jassert(false);
		return false;
	}

	int PatchDatabase::deletePatches(PatchFilter filter)
	{
		return impl->deletePatches(filter);
	}

	int PatchDatabase::reindexPatches(PatchFilter filter)
	{
		return impl->reindexPatches(filter);
	}

	std::vector<PatchHolder> PatchDatabase::getPatches(PatchFilter filter, int skip, int limit)
	{
		std::vector<PatchHolder> result;
		std::vector<std::pair<std::string, PatchHolder>> faultyIndexedPatches;
		bool success = impl->getPatches(filter, result, faultyIndexedPatches, skip, limit);
		if (success) {
			if (!faultyIndexedPatches.empty()) {
				SimpleLogger::instance()->postMessage((boost::format("Found %d patches with inconsistent MD5 - please run the Edit... Reindex Patches command for this synth") % faultyIndexedPatches.size()).str());
			}
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

	size_t PatchDatabase::mergePatchesIntoDatabase(std::vector<PatchHolder> &patches, std::vector<PatchHolder> &outNewPatches, ProgressHandler *progress, unsigned updateChoice)
	{
		return impl->mergePatchesIntoDatabase(patches, outNewPatches, progress, updateChoice, true);
	}

	std::vector<ImportInfo> PatchDatabase::getImportsList(Synth *activeSynth) const {
		if (activeSynth) {
			return impl->getImportsList(activeSynth);
		}
		else {
			return {};
		}
	}

	std::string PatchDatabase::generateDefaultDatabaseLocation() {
		auto knobkraft = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("KnobKraft");
		if (!knobkraft.exists()) {
			knobkraft.createDirectory();
		}
		return knobkraft.getChildFile(kDataBaseFileName).getFullPathName().toStdString();
	}

	std::string PatchDatabase::makeDatabaseBackup(std::string const &suffix) {
		return impl->makeDatabaseBackup(suffix);
	}

	void PatchDatabase::makeDatabaseBackup(File backupFileToCreate)
	{
		impl->makeDatabaseBackup(backupFileToCreate);
	}

	midikraft::PatchDatabase::PatchFilter PatchDatabase::allForSynth(std::shared_ptr<Synth> synth)
	{
		PatchFilter filter;
		filter.onlyFaves = false;
		filter.onlySpecifcType = false;
		filter.onlyUntagged = false;
		filter.showHidden = true;
		filter.synths.emplace(synth->getName(), synth);
		return filter;
	}

	}

