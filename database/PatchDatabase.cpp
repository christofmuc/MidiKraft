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

#include "FileHelpers.h"

#include <iostream>
#include <boost/format.hpp>

#include "SQLiteCpp/Database.h"
#include "SQLiteCpp/Statement.h"
#include "SQLiteCpp/Transaction.h"

namespace midikraft {

	const std::string kDataBaseFileName = "SysexDatabaseOfAllPatches.db3";
	const std::string kDataBaseBackupSuffix = "-backup";

	const int SCHEMA_VERSION = 4;
	/* History */
	/* 1 - Initial schema */
	/* 2 - adding hidden flag (aka deleted) */
	/* 3 - adding type integer to patch (to differentiate voice, patch, layer, tuning...) */
	/* 4 - forgot to migrate existing data NULL to 0 */

	class PatchDatabase::PatchDataBaseImpl {
	public:
		PatchDataBaseImpl(std::string const &databaseFile) : db_(databaseFile.c_str(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE) {
			createSchema();
			manageBackupDiskspace(kDataBaseBackupSuffix);
		}

		~PatchDataBaseImpl() {
			PatchDataBaseImpl::makeDatabaseBackup(kDataBaseBackupSuffix);
		}

		void makeDatabaseBackup(String const &suffix) {
			File dbFile(db_.getFilename());
			if (dbFile.existsAsFile()) {				
				File backupCopy(dbFile.getParentDirectory().getNonexistentChildFile(dbFile.getFileNameWithoutExtension() + suffix, dbFile.getFileExtension(), false));
				db_.backup(backupCopy.getFullPathName().toStdString().c_str(), SQLite::Database::Save);
			}
			else {
				jassertfalse;
			}
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
				} else {
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
				SQLite::Statement sql(db_, "INSERT INTO patches (synth, md5, name, type, data, favorite, hidden, sourceID, sourceName, sourceInfo, midiProgramNo, categories, categoryUserDecision)"
					" VALUES (:SYN, :MD5, :NAM, :TYP, :DAT, :FAV, :HID, :SID, :SNM, :SRC, :PRG, :CAT, :CUD)");

				// Insert values into prepared statement
				sql.bind(":SYN", patch.synth()->getName().c_str());
				sql.bind(":MD5", patch.md5());
				sql.bind(":NAM", patch.name());
				sql.bind(":TYP", patch.getType());
				sql.bind(":DAT", patch.patch()->data().data(), (int) patch.patch()->data().size());
				sql.bind(":FAV", (int)patch.howFavorite().is());
				sql.bind(":HID", patch.isHidden());
				sql.bind(":SID", sourceID);
				sql.bind(":SNM", patch.sourceInfo()->toDisplayString(patch.synth(), false));
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

		bool getPatches(PatchFilter filter, std::vector<PatchHolder> &result, int skip, int limit) {
			std::string selectStatement = "SELECT * FROM patches " + buildWhereClause(filter) + " ORDER BY sourceID, midiProgramNo ";
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
				std::shared_ptr<DataFile> newPatch;

				// Find the synth this patch is for
				auto synthName = query.getColumn("synth");
				if (filter.synths.find(synthName) == filter.synths.end()) {
					SimpleLogger::instance()->postMessage((boost::format("Program error, query returned patch for synth %s which was not part of the filter") % synthName).str());
					continue;
				}
				auto thisSynth = filter.synths[synthName].lock();

				// Create the patch itself, from the BLOB stored
				auto dataColumn = query.getColumn("data");
				if (dataColumn.isBlob()) {
					std::vector<uint8> patchData((uint8 *)dataColumn.getBlob(), ((uint8 *)dataColumn.getBlob()) + dataColumn.getBytes());
					
					int midiProgramNumber = query.getColumn("midiProgramNo").getInt();
					newPatch = thisSynth->patchFromPatchData(patchData, MidiProgramNumber::fromZeroBase(midiProgramNumber));
				}

				if (newPatch) {
					auto sourceColumn = query.getColumn("sourceInfo");
					if (sourceColumn.isText()) {
						PatchHolder holder(thisSynth, SourceInfo::fromString(sourceColumn.getString()), newPatch, false);

						std::string patchName = query.getColumn("name").getString();
						holder.setName(patchName);

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

		std::map<std::string, PatchHolder> bulkGetPatches(std::vector<PatchHolder> & patches, ProgressHandler *progress) {
			// Query the database for exactly those patches, we want to know which ones are already there!
			std::map<std::string, PatchHolder> result;

			int checkedForExistance = 0;
			for (auto ph : patches) {
				if (progress && progress->shouldAbort()) return std::map<std::string, PatchHolder>();
				// First, calculate list of "IDs"
				// Query the database if this exists. Normally, I would bulk, but as the database is local for now I think we're going to be fine
				try {
					std::string md5 = ph.md5();
					SQLite::Statement query(db_, "SELECT md5, name FROM patches WHERE md5 = :MD5 and synth = :SYN");
					query.bind(":SYN", ph.synth()->getName());
					query.bind(":MD5", md5);
					if (query.executeStep()) {
						std::string name = query.getColumn("name");
						ph.setName(name);
						result.emplace(md5, ph);
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

		void updatePatch(PatchHolder newPatch, PatchHolder existingPatch, unsigned updateChoices) {
			// For now, only run an update query if the newPatch Favorite is different from the database Favorite and is not "don't know"
			if (newPatch.howFavorite().is() != Favorite::TFavorite::DONTKNOW) {
				SQLite::Statement sql(db_, "UPDATE patches SET favorite = :FAV WHERE md5 = :MD5 and synth = :SYN");
				sql.bind(":SYN", existingPatch.synth()->getName());
				sql.bind(":FAV", (int) newPatch.howFavorite().is());
				sql.bind(":MD5", newPatch.md5());
				if (sql.exec() != 1) {
					//TODO - this would happen e.g. if the database is locked, because somebody is trying to modify it with DB browser (me for instance)
					jassert(false);
					throw new std::runtime_error("FATAL, I don't want to ruin your database");
				}
			}

			// Also, update the categories bit vector and the hidden field if at least one of the updateChoices is set
			if (updateChoices) { 
				std::string updateClause;
				if (updateChoices & UPDATE_CATEGORIES) updateClause = prependWithComma(updateClause, "categories = :CAT, categoryUserDecision = :CUD");
				if (updateChoices & UPDATE_NAME) updateClause = prependWithComma(updateClause, "name = :NAM");
				if (updateChoices & UPDATE_HIDDEN) updateClause = prependWithComma(updateClause, "hidden = :HID");
				if (updateChoices & UPDATE_DATA) updateClause = prependWithComma(updateClause, "data = :DAT");
				
				SQLite::Statement sql(db_, "UPDATE patches SET " + updateClause + " WHERE md5 = :MD5");
				if (updateChoices & UPDATE_CATEGORIES) {
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
				sql.bind(":MD5", newPatch.md5());
				if (sql.exec() != 1) {
					jassert(false);
					throw new std::runtime_error("FATAL, I don't want to ruin your database");
				}
			}
		}

		bool hasDefaultName(DataFile *patch) {
			auto storedPatchNameCapa = dynamic_cast<StoredPatchNameCapability *>(patch);
			if (storedPatchNameCapa) {
				return storedPatchNameCapa->isDefaultName();
			}
			return false;
		}

		size_t mergePatchesIntoDatabase(std::vector<PatchHolder> &patches, std::vector<PatchHolder> &outNewPatches, ProgressHandler *progress, unsigned updateChoice) {
			// This works by doing a bulk get operation for the patches from the database...
			auto knownPatches = bulkGetPatches(patches, progress);

			SQLite::Transaction transaction(db_); 

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

					// Update the database with the new info
					updatePatch(patch, knownPatches[md5_key], onlyUpdateThis);
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
						importName = newPatch.sourceInfo()->toDisplayString(newPatch.synth(), false);
					}
				}
			}

			//TODO can be replaced by repaired bulkPut
			std::map<String, PatchHolder> md5Inserted;
			std::map<Synth *, int> synthsWithUploadedItems;
			int sumOfAll = 0;
			for (auto newPatch : outNewPatches) {
				if (progress && progress->shouldAbort()) return sumOfAll;
				if (md5Inserted.find(newPatch.md5()) != md5Inserted.end()) {
					auto duplicate = md5Inserted[newPatch.md5()];

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
					putPatch(newPatch, source_id);
					md5Inserted[newPatch.md5()] = newPatch;
					sumOfAll++;
					if (synthsWithUploadedItems.find(newPatch.synth()) == synthsWithUploadedItems.end()) {
						// First time this synth sees an upload
						synthsWithUploadedItems[newPatch.synth()] = 0;
					}
					synthsWithUploadedItems[newPatch.synth()] += 1;
				}
				if (progress) progress->setProgressPercentage(sumOfAll / (double)outNewPatches.size());
			}

			for (auto synth : synthsWithUploadedItems) {
				if (synth.second > 0) {
					// Check if this import already exists (this should only happen with the EditBufferImport special case)
					bool alreadyExists = false;
					SQLite::Statement query(db_, "SELECT count(*) AS numExisting FROM imports WHERE synth = :SYN and id = :SID");
					query.bind(":SYN", synth.first->getName());
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
						sql.bind(":SYN", synth.first->getName());
						sql.bind(":NAM", importName);
						sql.bind(":SID", source_id);
						sql.exec();
					}
				}
			}

			transaction.commit();

			return sumOfAll;
		}

		int deletePatches(PatchFilter filter) {
			SQLite::Transaction transaction(db_);

			// Build a delete query
			std::string deleteStatement = "DELETE FROM patches " + buildWhereClause(filter);
			SQLite::Statement query(db_, deleteStatement.c_str());
			bindWhereClause(query, filter);

			// Execute
			int rowsDeleted = query.exec();

			transaction.commit();

			return rowsDeleted;
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
		return impl->mergePatchesIntoDatabase(newPatches, insertedPatches, nullptr, UPDATE_ALL);
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

	size_t PatchDatabase::mergePatchesIntoDatabase(std::vector<PatchHolder> &patches, std::vector<PatchHolder> &outNewPatches, ProgressHandler *progress, unsigned updateChoice)
	{
		return impl->mergePatchesIntoDatabase(patches, outNewPatches, progress, updateChoice);
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



}

