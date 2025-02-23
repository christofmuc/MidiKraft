/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchDatabase.h"

#include "Capability.h"
#include "Patch.h"
#include "Logger.h"
#include "PatchHolder.h"
#include "SynthBank.h"
#include "StoredPatchNameCapability.h"
#include "HasBanksCapability.h"

#include "JsonSchema.h"
#include "JsonSerialization.h"

#include "ProgressHandler.h"

#include "FileHelpers.h"

#include <iostream>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include "SpdLogJuce.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>

#include <SQLiteCpp/../../sqlite3/sqlite3.h> //TODO How to use the underlying site3 correctly?

namespace midikraft {

	const std::string kDataBaseFileName = "SysexDatabaseOfAllPatches.db3";
	const std::string kDataBaseBackupSuffix = "-backup";

	const int SCHEMA_VERSION = 14;
	/* History */
	/* 1 - Initial schema */
	/* 2 - adding hidden flag (aka deleted) */
	/* 3 - adding type integer to patch (to differentiate voice, patch, layer, tuning...) */
	/* 4 - forgot to migrate existing data NULL to 0 */
	/* 5 - adding bank number column for better sorting of multi-imports */
	/* 6 - adding the table categories to track which bit index is used for which tag */
	/* 7 - adding the table lists to allow storing lists of patches */
	/* 8 - adding synth name, timestamp and banknumber to patch list to allow store synth banks */
	/* 9 - adding foreign key to make sure no patch is deleted that belongs to a list */
	/* 10 - drop tables created by upgrade to 9, needing retry with database connection */
	/* 11 - adding an index to speed up the duplicate name search, as suggested by chatGPT */
	/* 12 - adding an index to speed up the import list building */
	/* 13 - adding comment to the patch table */
	/* 14 - adding author and source fields to the patch table */

	class PatchDatabase::PatchDataBaseImpl {
	public:
		PatchDataBaseImpl(std::string const& databaseFile, OpenMode mode)
			: db_(databaseFile.c_str(), mode == OpenMode::READ_ONLY ? SQLite::OPEN_READONLY : (SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)), bitfield({}),
			mode_(mode)
		{
			createSchema();
			manageBackupDiskspace(kDataBaseBackupSuffix);
			categoryDefinitions_ = getCategories();
		}

		~PatchDataBaseImpl() {
			// Only make the automatic database backup when we are not in read only mode, else there is nothing to backup
			if (mode_ == OpenMode::READ_WRITE) {
				PatchDataBaseImpl::makeDatabaseBackup(kDataBaseBackupSuffix);
			}
		}

		std::string makeDatabaseBackup(String const& suffix) {
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

		static void makeDatabaseBackup(File database, File backupFile) {
			SQLite::Database db(database.getFullPathName().toStdString().c_str(), SQLite::OPEN_READONLY);
			db.backup(backupFile.getFullPathName().toStdString().c_str(), SQLite::Database::Save);
		}

		void backupIfNecessary(bool& done) {
			if (!done && mode_ == PatchDatabase::OpenMode::READ_WRITE) {
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
						spdlog::error("Error - failed to remove extra backup file, please check file permissions: {}", file.getFullPathName());
					}
				}
				else {
					numKept++;
					keptBackupSize += file.getSize();
				}
			}
			if (backupSize != keptBackupSize) {
				spdlog::info("Removing all but {} backup files reducing disk space used from {} to {} bytes", numKept, backupSize, keptBackupSize);
			}
		}

		std::string migrateTable(std::string table_name, std::function<void()> create_new_table, std::vector<std::string> const& column_list) {
			auto old_table_name = table_name + "_old";
			db_.exec(fmt::format("ALTER TABLE {} RENAME TO {}", table_name, old_table_name ).c_str());
			create_new_table();
			std::string column_names;
			for (auto const& name : column_list) {
				if (!column_names.empty()) {
					column_names += ", ";
				}
				column_names += name;
			}
			auto query = fmt::format("INSERT INTO {}({}) SELECT {} FROM {}",  table_name, column_names, column_names, old_table_name);
			db_.exec(query.c_str());			
			return old_table_name;
		}

		void migrateSchema(int currentVersion) {
			bool hasBackuped = false;
			bool hasRecreatedPatchTable = false;

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
			if (currentVersion < 6) {
				backupIfNecessary(hasBackuped);
				SQLite::Transaction transaction(db_);
				if (!db_.tableExists("categories")) {
					//TODO This code should actually never be executed, because the createSchema() already has created the table. Create Table statements don't belong into the migrteSchema method
					db_.exec("CREATE TABLE categories (bitIndex INTEGER UNIQUE, name TEXT, color TEXT, active INTEGER)");
					insertDefaultCategories();
				}
				db_.exec("UPDATE schema_version SET number = 6");
				transaction.commit();
			}
			if (currentVersion < 7) {
				backupIfNecessary(hasBackuped);
				SQLite::Transaction transaction(db_);
				if (!db_.tableExists("lists")) {
					db_.exec("CREATE TABLE IF NOT EXISTS lists(id TEXT PRIMARY KEY, name TEXT NOT NULL)");
				}
				if (!db_.tableExists("patch_in_list")) {
					db_.exec("CREATE TABLE IF NOT EXISTS patch_in_list(id TEXT NOT NULL, synth TEXT NOT NULL, md5 TEXT NOT NULL, order_num INTEGER NOT NULL)");
				}
				// Bonus upgrade - should this be a database from the early lists experiments, the order_num column is empty and it needs to be calculated!
				db_.exec("WITH po AS (SELECT *, ROW_NUMBER() OVER(PARTITION BY id) -1 AS new_order FROM patch_in_list) "
					"UPDATE patch_in_list AS pl SET order_num = (SELECT new_order FROM po WHERE pl.id = po.id AND pl.synth = po.synth AND pl.md5 = po.md5)");
				db_.exec("UPDATE schema_version SET number = 7");
				transaction.commit();
			}
			if (currentVersion < 8) {
				backupIfNecessary(hasBackuped);
				SQLite::Transaction transaction(db_);
				try {
					db_.exec("ALTER TABLE lists ADD COLUMN synth TEXT");
					db_.exec("ALTER TABLE lists ADD COLUMN midi_bank_number INTEGER");
					db_.exec("ALTER TABLE lists ADD COLUMN last_synced INTEGER");
				}
				catch (SQLite::Exception& e) {
					spdlog::warn("Could not add additional columns into lists table, database already partially migrated? {}", e.getErrorStr());
				}
				db_.exec("UPDATE schema_version SET number = 8");
				transaction.commit();
			}
			if (currentVersion < 9) {
				backupIfNecessary(hasBackuped);
				db_.exec("PRAGMA foreign_keys = OFF");
				SQLite::Transaction transaction(db_);
				auto table1 = migrateTable("patches", std::bind(&PatchDataBaseImpl::createPatchTable, this),
					{ "synth", "md5", "name", "data", "favorite", "sourceID", "sourceName", "sourceInfo", "midiProgramNo", "categories", "categoryUserDecision", "hidden", "type", "midiBankNo" });
				auto table2 = migrateTable("patch_in_list", std::bind(&PatchDataBaseImpl::createPatchInListTable, this),
					{ "id", "synth", "md5", "order_num" });
				hasRecreatedPatchTable = true;
				db_.exec("UPDATE schema_version SET number = 9");
				transaction.commit();
			}
			if (currentVersion < 10) {
				backupIfNecessary(hasBackuped);
				db_.exec("PRAGMA foreign_keys = OFF");
				/// These can't be deleted within a transaction
				jassert(db_.getTotalChanges() == 0);
				db_.exec(fmt::format("DROP TABLE IF EXISTS {}", "patches_old").c_str());
				db_.exec(fmt::format("DROP TABLE IF EXISTS {}", "patch_in_list_old").c_str());
				db_.exec("UPDATE schema_version SET number = 10");
				db_.exec("PRAGMA foreign_keys = ON");
				db_.exec("VACUUM");
			}
			if (currentVersion < 11) {
				backupIfNecessary(hasBackuped);
				SQLite::Transaction transaction(db_);
				/// These can't be deleted within a transaction
				db_.exec("CREATE INDEX IF NOT EXISTS patch_synth_name_idx ON patches (synth, name)");
				db_.exec("UPDATE schema_version SET number = 11");
				transaction.commit();
			}
			if (currentVersion < 12) {
				backupIfNecessary(hasBackuped);
				SQLite::Transaction transaction(db_);
				/// These can't be deleted within a transaction
				db_.exec("CREATE INDEX IF NOT EXISTS patch_sourceid_idx ON patches (sourceID)");
				db_.exec("UPDATE schema_version SET number = 12");
				transaction.commit();
			}
			if (currentVersion < 13) {
				backupIfNecessary(hasBackuped);
				SQLite::Transaction transaction(db_);
				if (!hasRecreatedPatchTable) {
					db_.exec("ALTER TABLE patches ADD COLUMN comment TEXT");
				}
				db_.exec("UPDATE schema_version SET number = 13");
				transaction.commit();
			}
			if (currentVersion < 14) {
				backupIfNecessary(hasBackuped);
				SQLite::Transaction transaction(db_);
				if (!hasRecreatedPatchTable) {
					db_.exec("ALTER TABLE patches ADD COLUMN author TEXT");
					db_.exec("ALTER TABLE patches ADD COLUMN info TEXT");
				}
				db_.exec("UPDATE schema_version SET number = 14");
				transaction.commit();
			}
		}

		void insertDefaultCategories() {
			db_.exec(String("INSERT INTO categories VALUES (0, 'Lead', '" + Colour::fromString("ff8dd3c7").darker().toString() + "', 1)").toStdString().c_str());
			db_.exec(String("INSERT INTO categories VALUES (1, 'Pad', '" + Colour::fromString("ffffffb3").darker().toString() + "', 1)").toStdString().c_str());
			db_.exec(String("INSERT INTO categories VALUES (2, 'Brass', '" + Colour::fromString("ff4a75b2").darker().toString() + "', 1)").toStdString().c_str());
			db_.exec(String("INSERT INTO categories VALUES (3, 'Organ', '" + Colour::fromString("fffb8072").darker().toString() + "', 1)").toStdString().c_str());
			db_.exec(String("INSERT INTO categories VALUES (4, 'Keys', '" + Colour::fromString("ff80b1d3").darker().toString() + "', 1)").toStdString().c_str());
			db_.exec(String("INSERT INTO categories VALUES (5, 'Bass', '" + Colour::fromString("fffdb462").darker().toString() + "', 1)").toStdString().c_str());
			db_.exec(String("INSERT INTO categories VALUES (6, 'Arp', '" + Colour::fromString("ffb3de69").darker().toString() + "', 1)").toStdString().c_str());
			db_.exec(String("INSERT INTO categories VALUES (7, 'Pluck', '" + Colour::fromString("fffccde5").darker().toString() + "', 1)").toStdString().c_str());
			db_.exec(String("INSERT INTO categories VALUES (8, 'Drone', '" + Colour::fromString("ffd9d9d9").darker().toString() + "', 1)").toStdString().c_str());
			db_.exec(String("INSERT INTO categories VALUES (9, 'Drum', '" + Colour::fromString("ffbc80bd").darker().toString() + "', 1)").toStdString().c_str());
			db_.exec(String("INSERT INTO categories VALUES (10, 'Bell', '" + Colour::fromString("ffccebc5").darker().toString() + "', 1)").toStdString().c_str());
			db_.exec(String("INSERT INTO categories VALUES (11, 'SFX', '" + Colour::fromString("ffffed6f").darker().toString() + "', 1)").toStdString().c_str());
			db_.exec(String("INSERT INTO categories VALUES (12, 'Ambient', '" + Colour::fromString("ff869cab").darker().toString() + "', 1)").toStdString().c_str());
			db_.exec(String("INSERT INTO categories VALUES (13, 'Wind', '" + Colour::fromString("ff317469").darker().toString() + "', 1)").toStdString().c_str());
			db_.exec(String("INSERT INTO categories VALUES (14, 'Voice', '" + Colour::fromString("ffa75781").darker().toString() + "', 1)").toStdString().c_str());
		}

		void createPatchTable() {
			db_.exec("CREATE TABLE IF NOT EXISTS patches (synth TEXT NOT NULL, md5 TEXT NOT NULL, name TEXT, type INTEGER, data BLOB, favorite INTEGER, hidden INTEGER, sourceID TEXT, sourceName TEXT,"
				" sourceInfo TEXT, midiBankNo INTEGER, midiProgramNo INTEGER, categories INTEGER, categoryUserDecision INTEGER, comment TEXT, author TEXT, info TEXT PRIMARY KEY (synth, md5))");
		}

		void createPatchInListTable() {
			db_.exec("CREATE TABLE IF NOT EXISTS patch_in_list(id TEXT NOT NULL, synth TEXT NOT NULL, md5 TEXT NOT NULL, order_num INTEGER NOT NULL, FOREIGN KEY(synth, md5) REFERENCES patches(synth, md5))");
		}

		void createSchema() {
			db_.exec("PRAGMA foreign_keys = ON");

			SQLite::Transaction transaction(db_);
			if (!db_.tableExists("patches")) {
				createPatchTable();
			}
			if (!db_.tableExists("imports")) {
				db_.exec("CREATE TABLE IF NOT EXISTS imports (synth TEXT, name TEXT, id TEXT, date TEXT)");
			}
			if (!db_.tableExists("categories")) {
				db_.exec("CREATE TABLE IF NOT EXISTS categories (bitIndex INTEGER UNIQUE, name TEXT, color TEXT, active INTEGER)");
				insertDefaultCategories();

			}
			if (!db_.tableExists("schema_version")) {
				db_.exec("CREATE TABLE IF NOT EXISTS schema_version (number INTEGER)");
			}
			if (!db_.tableExists("lists")) {
				db_.exec("CREATE TABLE IF NOT EXISTS lists(id TEXT PRIMARY KEY, name TEXT NOT NULL, synth TEXT, midi_bank_number INTEGER, last_synced INTEGER)");
			}
			
			if (!db_.tableExists("patch_in_list")) {
				createPatchInListTable();
			}

			// Creating indexes
			db_.exec("CREATE INDEX IF NOT EXISTS patch_synth_name_idx ON patches (synth, name)");
			db_.exec("CREATE INDEX IF NOT EXISTS patch_sourceid_idx ON patches (sourceID)");

			// Commit transaction
			transaction.commit();

			// Check if schema needs to be migrated
			std::unique_ptr<SQLite::Statement> schemaQuery = std::make_unique<SQLite::Statement>(db_, "SELECT number FROM schema_version");
			if (schemaQuery->executeStep()) {
				int version = schemaQuery->getColumn("number").getInt();
				if (version < SCHEMA_VERSION) {
					try {
						// Explicitly close our query, because we might want to drop tables and can do this only with closed operations
						schemaQuery.reset();
						migrateSchema(version);
					}
					catch (SQLite::Exception& e) {
						if (mode_ == OpenMode::READ_WRITE) {
							std::string message = fmt::format("Cannot open database file {} - Cannot upgrade to latest version, schema version found is {}. Error: {}", db_.getFilename(), version, e.what());
							AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Failure to open database", message);
						}
						if (e.getErrorCode() == SQLITE_READONLY) {
							throw PatchDatabaseReadonlyException(e.what());
						}
						else {
							throw e;
						}
					}
				}
				else if (version > SCHEMA_VERSION) {
					// This is a database from the future, can't open!
					std::string message = fmt::format("Cannot open database file {} - this was produced with a newer version of KnobKraft Orm, schema version is {}.", db_.getFilename(), version);
					if (mode_ == OpenMode::READ_WRITE) {
						AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Database Error", message);
					}
					throw SQLite::Exception(message);
				}
			}
			else {
				// Ups, completely empty database, need to insert current schema version
				int rows = db_.exec("INSERT INTO schema_version VALUES (" + String(SCHEMA_VERSION).toStdString() + ")");
				if (rows != 1) {
					jassert(false);
					if (mode_ == OpenMode::READ_WRITE) {
						AlertWindow::showMessageBox(AlertWindow::WarningIcon, "SQL Error", "For whatever reason couldn't insert the schema version number. Something is terribly wrong.");
					}
				}
			}
		}

		bool putPatch(PatchHolder const& patch, std::string const& sourceID) {
			try {
				SQLite::Statement sql(db_, "INSERT INTO patches (synth, md5, name, type, data, favorite, hidden, sourceID, sourceName, sourceInfo, midiBankNo, midiProgramNo, categories, categoryUserDecision, comment, author, info)"
					" VALUES (:SYN, :MD5, :NAM, :TYP, :DAT, :FAV, :HID, :SID, :SNM, :SRC, :BNK, :PRG, :CAT, :CUD, :COM, :AUT, :INF)");

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
                if (patch.bankNumber().isValid()) {
                    sql.bind(":BNK", patch.bankNumber().toZeroBased());
                }
                else
                {
                    sql.bind(":BNK");
                }
				sql.bind(":PRG", patch.patchNumber().toZeroBasedWithBank());
				sql.bind(":CAT", (int64_t) bitfield.categorySetAsBitfield(patch.categories()));
				sql.bind(":CUD", (int64_t) bitfield.categorySetAsBitfield(patch.userDecisionSet()));
				sql.bind(":COM", patch.comment());
				sql.bind(":AUT", patch.author());
				sql.bind(":INF", patch.info());

				sql.exec();
			}
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in putPatch: SQL Exception {}", ex.what());
			}
			return true;
		}

		std::vector<ImportInfo> getImportsList(Synth* activeSynth) {
			SQLite::Statement query(db_, "SELECT imports.name, id, count(patches.md5) AS patchCount FROM imports JOIN patches on imports.id == patches.sourceID WHERE patches.synth = :SYN AND imports.synth = :SYN GROUP BY imports.id ORDER BY date");
			query.bind(":SYN", activeSynth->getName());
			std::vector<ImportInfo> result;
			while (query.executeStep()) {
				result.push_back({ query.getColumn("name").getText(), query.getColumn("id").getText(), query.getColumn("patchCount").getInt() });
			}
			return result;
		}

		bool renameImport(std::string synthName, std::string importID, std::string newName) {
			try {
				SQLite::Transaction transaction(db_);
				SQLite::Statement update(db_, "UPDATE imports set name = :NAM where id = :IID and synth = :SYN");
				update.bind(":NAM", newName);
				update.bind(":IID", importID);
				update.bind(":SYN", synthName);
				int rowsModified = update.exec();
				if (rowsModified == 1) {
					// Success
					transaction.commit();
					return true;
				}
				else if (rowsModified == 0) {
					spdlog::error("Failed to update import - not found with ID {}", importID);
					return false;
				}
				else {
					spdlog::error("Failed to update import, abort - more than one row found with ID {}", importID);
					return false;
				}
			}
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in renameImport: SQL Exception {}", ex.what());
				return false;
			}
		}

		std::string buildWhereClause(PatchFilter filter, bool needsCollate) {
			std::string where = " WHERE 1 == 1 ";
			if (!filter.synths.empty()) {
				//TODO does SQlite do "IN" clause?
				where += " AND ( ";
				int s = 0;
				for (auto synth : filter.synths) {
					if (s != 0) where += " OR ";
					where += "patches.synth = " + synthVariable(s++);
				}
				where += " ) ";
			}
			if (!filter.importID.empty()) {
				where += " AND sourceID = :SID";
			}
			if (!filter.name.empty()) {
				where += " AND (name LIKE :NAM or comment LIKE :NAM or author LIKE :NAM or info LIKE :NAM)";
				if (needsCollate) {
					where += " COLLATE NOCASE";
				}
			}
			if (!filter.listID.empty()) {
				where += " AND patch_in_list.id = :LID";
			}
			if (filter.onlySpecifcType) {
				where += " AND type == :TYP";
			}

			// Show Hidden and Show Faves are special in that they can be combined
			// and need explicit code for the 4 cases
			if (filter.onlyFaves) {
				if (filter.showHidden) {
					if (filter.showUndecided) {
						// No op, just retrieve all
					}
					else
					{
						// Don't show all
						where += " AND (hidden == 1 OR favorite == 1)";
					}
				}
				else
				{
					if (filter.showUndecided) {
						// Everything that is not hidden
						where += " AND (hidden is null or hidden != 1)";
					}
					else
					{
						// Only favorites that are not hidden
						where += " AND (favorite == 1) AND (hidden is null or hidden != 1)";
					}
				}
			}
			else {
				if (filter.showHidden) {
					if (filter.showUndecided) {
						// All that's not favorite
						where += " AND (favorite != 1)";
					}
					else
					{
						// Only hidden
						where += " AND (hidden == 1)";
					}
				}
				else
				{
					if (filter.showUndecided) {
						// Everything that is not hidden and not fave
						where += " AND (favorite != 1) AND (hidden is null or hidden != 1)";
					}
					else
					{
						// All that is not hidden
						where += " AND (hidden is null or hidden != 1)";
					}
				}
			}

			if (filter.onlyUntagged) {
				where += " AND categories == 0";
			}
			else if (!filter.categories.empty()) {
				// Empty category filter set will of course return everything
				//TODO this has bad query performance as it will force a table scan, but for now I cannot see this becoming a problem as long as the database is not multi-tenant
				// The correct way to do this would be to create a many to many relationship and run an "exists" query or join/unique the category table. Returning the list of categories also requires 
				// a concat on sub-query, so we're running into more complex SQL territory here.
				if (!filter.andCategories) {
					where += " AND (categories & :CAT != 0)";
				}
				else {
					where += " AND (categories & :CAT == :CAT)";
				}
			}
			if (filter.onlyDuplicateNames) {
				where += " AND patches_count.count > 1";
			}
			//spdlog::debug(where);
			return where;
		}

		std::string buildOrderClause(PatchFilter filter) {
			std::string orderByClause;
			switch (filter.orderBy) {
			case PatchOrdering::No_ordering: orderByClause = ""; break;
			case PatchOrdering::Order_by_Import_id: orderByClause = " ORDER BY sourceID, midiBankNo, midiProgramNo ";; break;
			case PatchOrdering::Order_by_Name: orderByClause = " ORDER BY name, midiBankNo, midiProgramNo "; break;
			case PatchOrdering::Order_by_Place_in_List: orderByClause = " ORDER BY order_num"; break;
			case PatchOrdering::Order_by_ProgramNo: orderByClause = " ORDER BY midiProgramNo, name"; break;
			case PatchOrdering::Order_by_BankNo: orderByClause = " ORDER BY midiBankNo, midiProgramNo, name"; break;
			default:
				jassertfalse;
				spdlog::error("Program error - encountered invalid ordering field in buildOrderClause");
			}
			return orderByClause;
		}

		std::string buildJoinClause(PatchFilter filter, bool outer_join = false) {
			// If we are also filtering for a list, we need to join the patch_in_list table!
			std::string joinClause = "";
			if (!filter.listID.empty() || outer_join) {
				if (outer_join) {
					joinClause += " LEFT JOIN ";
				}
				else {
					joinClause += " INNER JOIN ";
				}
				joinClause += "patch_in_list ON patches.md5 = patch_in_list.md5 AND patches.synth = patch_in_list.synth";
			}
			if (filter.onlyDuplicateNames) {
				if (outer_join) {
					joinClause += " LEFT JOIN ";
				}
				else {
					joinClause += " INNER JOIN ";
				}
				joinClause += " patches_count ON patches.synth = patches_count.synth AND patches.name = patches_count.dup_name";
				/*if (filter.showHidden)
					joinClause += " JOIN (select name as dup_name, synth, count(*) as name_count from patches group by dup_name, synth) as ordinal_table on patches.name = ordinal_table.dup_name and patches.synth = ordinal_table.synth";
				else
					joinClause += " JOIN (select name as dup_name, synth, count(*) as name_count from patches where hidden = 0 group by dup_name, synth) as ordinal_table on patches.name = ordinal_table.dup_name and patches.synth = ordinal_table.synth";*/
			}
			return joinClause;
		}

		std::string buildCTE(PatchFilter filter) {
			if (filter.onlyDuplicateNames) {
				return R"sql(WITH patches_count AS (
   SELECT synth, name as dup_name, COUNT(*) as count
   FROM patches
   GROUP BY synth, name
))sql";
			}
			else {
				return "";
			}
		}

		std::string synthVariable(int no) {
			// Calculate a variable name to bind the synth name to. This will blow up if you query for more than 99 synths.
			return fmt::format(":S{:02d}", no);
		}

		void bindWhereClause(SQLite::Statement& query, PatchFilter filter) {
			int s = 0;
			for (auto const& synth : filter.synths) {
				query.bind(synthVariable(s++), synth.second.lock()->getName());
			}
			if (!filter.importID.empty()) {
				query.bind(":SID", filter.importID);
			}
			if (!filter.listID.empty()) {
				query.bind(":LID", filter.listID);
			}
			if (!filter.name.empty()) {
				query.bind(":NAM", "%" + filter.name + "%");
			}
			if (filter.onlySpecifcType) {
				query.bind(":TYP", filter.typeID);
			}
			if (!filter.onlyUntagged && !filter.categories.empty()) {
				query.bind(":CAT", (int64_t) bitfield.categorySetAsBitfield(filter.categories));
			}
		}

		int getPatchesCount(PatchFilter filter) {
			try {
				std::string queryString = fmt::format("{} SELECT count(*) FROM patches {} {}", buildCTE(filter), buildJoinClause(filter), buildWhereClause(filter, false));
				SQLite::Statement query(db_, queryString);
				bindWhereClause(query, filter);
				if (query.executeStep()) {
					int count = query.getColumn(0).getInt();
					return count;
				}
			}
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in getPatchesCount: SQL Exception {}", ex.what());
			}
			return 0;
		}

		std::vector<Category> getCategories() {
			ScopedLock lock(categoryLock_);
			SQLite::Statement query(db_, "SELECT * FROM categories ORDER BY bitIndex");
			std::vector<std::shared_ptr<CategoryDefinition>> activeDefinitions;
			std::vector<Category> allCategories;
			while (query.executeStep()) {
				auto bitIndex = query.getColumn("bitIndex").getInt();
				auto name = query.getColumn("name").getText();
				auto colorName = query.getColumn("color").getText();
				bool isActive = query.getColumn("active").getInt() != 0;

				// Check if this already exists!
				bool found = false;
				for (auto exists : categoryDefinitions_) {
					if (exists.def()->id == bitIndex) {
						found = true;
						exists.def()->color = Colour::fromString(colorName);
						exists.def()->name = name;
						exists.def()->isActive = isActive;
						allCategories.push_back(exists);
						if (isActive) {
							activeDefinitions.emplace_back(exists.def());
						}
						break;
					}
				}
				if (!found) {
					auto def = std::make_shared<CategoryDefinition>(CategoryDefinition({ bitIndex, isActive, name, Colour::fromString(colorName) }));
					allCategories.push_back(Category(def));
					if (isActive) {
						activeDefinitions.emplace_back(def);
					}
				}
			}
			bitfield = CategoryBitfield(activeDefinitions); //TODO smell, side effect
			return allCategories;
		}

		int getNextBitindex() {
			SQLite::Statement query(db_, "SELECT MAX(bitIndex) + 1 as maxbitindex FROM categories");
			if (query.executeStep()) {
				int maxbitindex = query.getColumn("maxbitindex").getInt();
				if (maxbitindex < 63) {
					// That'll work!
					return maxbitindex;
				}
				else {
					spdlog::warn("You have exhausted the 63 possible categories, it is no longer possible to create new ones in this database. Consider splitting the database via PatchInterchangeFormat files");
					return -1;
				}
			}
			spdlog::error("Unexpected program error determining the next bit index!");
			return -1;
		}

		void updateCategories(std::vector<CategoryDefinition> const& newdefs) {
			try {
				SQLite::Transaction transaction(db_);

				for (auto c : newdefs) {
					// Check if insert or update
					SQLite::Statement query(db_, "SELECT * FROM categories WHERE bitIndex = :BIT");
					query.bind(":BIT", c.id);
					if (query.executeStep()) {
						// Bit index already exists, this is an update
						SQLite::Statement sql(db_, "UPDATE categories SET name = :NAM, color = :COL, active = :ACT WHERE bitindex = :BIT");
						sql.bind(":BIT", c.id);
						sql.bind(":NAM", c.name);
						sql.bind(":COL", c.color.toString().toStdString());
						sql.bind(":ACT", c.isActive);
						sql.exec();
					}
					else {
						// Doesn't exist, insert!
						SQLite::Statement sql(db_, "INSERT INTO categories (bitIndex, name, color, active) VALUES(:BIT, :NAM, :COL, :ACT)");
						sql.bind(":BIT", c.id);
						sql.bind(":NAM", c.name);
						sql.bind(":COL", c.color.toString().toStdString());
						sql.bind(":ACT", c.isActive);
						sql.exec();
					}
				}
				transaction.commit();
				// Refresh our internal data 
				categoryDefinitions_ = getCategories();
			}
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in updateCategories: SQL Exception {}", ex.what());
			}
		}

		void loadBankAndProgram(std::shared_ptr<Synth> synth, SQLite::Statement& query, MidiBankNumber& outBank, MidiProgramNumber& outProgram)
		{
			// Determine Bank (if stored) and Program 
			auto bankCol = query.getColumn("midiBankNo");
			int midiProgramNumber = query.getColumn("midiProgramNo").getInt();
			if (bankCol.isNull()) {
				outBank = MidiBankNumber::invalid();
				outProgram = MidiProgramNumber::fromZeroBase(midiProgramNumber);
			}
			else {
				outBank = MidiBankNumber::fromZeroBase(bankCol.getInt(), SynthBank::numberOfPatchesInBank(synth, bankCol.getInt()));
				outProgram = MidiProgramNumber::fromZeroBaseWithBank(outBank, midiProgramNumber);
			}
		}

		bool loadPatchFromQueryRow(std::shared_ptr<Synth> synth, SQLite::Statement& query, std::vector<PatchHolder>& result) {
			ScopedLock lock(categoryLock_);

			std::shared_ptr<DataFile> newPatch;

			// Create the patch itself, from the BLOB stored
			auto dataColumn = query.getColumn("data");


			MidiProgramNumber program = MidiProgramNumber::invalidProgram();
			MidiBankNumber bank = MidiBankNumber::invalid();
			loadBankAndProgram(synth, query, bank, program);

			// Load the BLOB
			if (dataColumn.isBlob()) {
			std::vector<uint8> patchData((uint8*)dataColumn.getBlob(), ((uint8*)dataColumn.getBlob()) + dataColumn.getBytes());
			//TODO I should not need the midiProgramNumber here
			newPatch = synth->patchFromPatchData(patchData, program);
			}

			// We need the current categories
			categoryDefinitions_ = getCategories();
			if (newPatch) {
				auto sourceColumn = query.getColumn("sourceInfo");
				if (sourceColumn.isText()) {
					PatchHolder holder(synth, SourceInfo::fromString(synth, sourceColumn.getString()), newPatch);
					holder.setBank(bank);
					holder.setPatchNumber(program);

					std::string patchName = query.getColumn("name").getString();
					holder.setName(patchName);
					std::string sourceId = query.getColumn("sourceID").getString();
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
					std::set<Category> updateSet;
					bitfield.makeSetOfCategoriesFromBitfield(updateSet, query.getColumn("categories").getInt64());
					holder.setCategories(updateSet);
					bitfield.makeSetOfCategoriesFromBitfield(updateSet, query.getColumn("categoryUserDecision").getInt64());
					holder.setUserDecisions(updateSet);

					auto commentColumn = query.getColumn("comment");
					if (commentColumn.isText()) {
						holder.setComment(std::string(commentColumn.getText()));
					}

					auto authorColumn = query.getColumn("author");
					if (authorColumn.isText()) {
						holder.setAuthor(std::string(authorColumn.getText()));
					}

					auto infoColumn = query.getColumn("info");
					if (infoColumn.isText()) {
						holder.setInfo(std::string(infoColumn.getText()));
					}

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

		bool getSinglePatch(std::shared_ptr<Synth> synth, std::string const& md5, std::vector<PatchHolder>& result) {
			try {
				SQLite::Statement query(db_, "SELECT * FROM patches WHERE md5 = :MD5 and synth = :SYN");
				query.bind(":SYN", synth->getName());
				query.bind(":MD5", md5);
				if (query.executeStep()) {
					return loadPatchFromQueryRow(synth, query, result);
				}
			}
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in getSinglePatch: SQL Exception {}", ex.what());
			}
			return false;
		}

		std::vector<MidiProgramNumber> getBankPositions(std::shared_ptr<Synth> synth, std::string const& md5) {
			std::vector<MidiProgramNumber> result;
			try {
				SQLite::Statement query(db_, "SELECT lists.midi_bank_number, pil.order_num FROM lists JOIN patch_in_list AS PIL ON lists.id = pil.id "
					"WHERE pil.md5 = :MD5 and lists.synth = :SYN AND lists.last_synced IS NOT NULL AND lists.last_synced > 0 AND lists.midi_bank_number IS NOT NULL");
				query.bind(":SYN", synth->getName());
				query.bind(":MD5", md5);
				while (query.executeStep()) {
					int bankNo = query.getColumn("midi_bank_number").getInt();
					if (auto descriptors = Capability::hasCapability<HasBankDescriptorsCapability>(synth)) {
						if (bankNo >= 0 && bankNo < descriptors->bankDescriptors().size()) {
							result.push_back(MidiProgramNumber::fromZeroBaseWithBank(MidiBankNumber::fromZeroBase(bankNo, descriptors->bankDescriptors()[bankNo].size), query.getColumn("order_num").getInt()));
						}
						else {
							spdlog::error("Data error - bank number stored is bigger than bank descriptors allow for!");
						}
					}
					else if (auto banks = Capability::hasCapability<HasBanksCapability>(synth)) {
						// All banks have the same size
						if (bankNo >= 0 && bankNo < banks->numberOfBanks()) {
							result.push_back(MidiProgramNumber::fromZeroBaseWithBank(MidiBankNumber::fromZeroBase(bankNo, banks->numberOfPatches()), query.getColumn("order_num").getInt()));
						}
						else {
							spdlog::error("Data error - bank number stored is bigger than banks count allows for!");
						}
					}
					else {
						spdlog::error("Data error - no way to determine MIDI Bank for list position");
					}
				}
			}
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in getSinglePatch: SQL Exception {}", ex.what());
			}
			return result;
		}

		bool getPatches(PatchFilter filter, std::vector<PatchHolder>& result, std::vector<std::pair<std::string, PatchHolder>>& needsReindexing, int skip, int limit) {
			std::string selectStatement = fmt::format("{} SELECT * FROM patches {} {} {}", buildCTE(filter), buildJoinClause(filter), buildWhereClause(filter, true), buildOrderClause(filter));
			spdlog::debug("SQL {}", selectStatement);
			if (limit != -1) {
				selectStatement += " LIMIT :LIM ";
				selectStatement += " OFFSET :OFS";
			}
			try {
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
						spdlog::error("Program error, query returned patch for synth {} which was not part of the filter", synthName.getString());
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
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in getPatches: SQL Exception {}", ex.what());
			}
			return false;
		}

		std::map<std::string, PatchHolder> bulkGetPatches(std::vector<PatchHolder> const& patches, ProgressHandler* progress) {
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
						MidiProgramNumber program = MidiProgramNumber::invalidProgram();
						MidiBankNumber bank = MidiBankNumber::invalid();
						loadBankAndProgram(ph.smartSynth(), query, bank, program);
						PatchHolder existingPatch(ph.smartSynth(), ph.sourceInfo(), nullptr);
						existingPatch.setBank(bank);
						existingPatch.setPatchNumber(program);
						std::string name = query.getColumn("name");
						existingPatch.setName(name);
						result.emplace(md5, existingPatch);
					}
				}
				catch (SQLite::Exception& ex) {
					spdlog::error("DATABASE ERROR in bulkGetPatches: SQL Exception {}", ex.what());
				}
				if (progress) progress->setProgressPercentage(checkedForExistance++ / (double)patches.size());
			}
			return result;
		}

		std::string prependWithComma(std::string const& target, std::string const& suffix) {
			if (target.empty())
				return suffix;
			return target + ", " + suffix;
		}

		void calculateMergedCategories(PatchHolder& newPatch, PatchHolder existingPatch) {
			// Now this is fun - we are adding information of a new Patch to an existing Patch. We will try to respect the user decision,
			// but as we in the reindexing case do not know whether the new or the existing has "better" information, we will just merge the existing categories and 
			// user decisions. Adding a category most often is more useful than removing one

			// Turn off existing user decisions where a new user decision exists
			auto newPatchesUserDecided = category_intersection(newPatch.categories(), newPatch.userDecisionSet());
			auto newPatchesAutomatic = category_difference(newPatch.categories(), newPatch.userDecisionSet());
			auto oldUserDecided = category_intersection(existingPatch.categories(), existingPatch.userDecisionSet());


			// The new categories are calculated as all categories from the new patch, unless there is a user decision at the existing patch not marked as overridden by a new user decision
			// plus all existing patch categories where there is no new user decision
			auto newAutomaticWithoutExistingOverride = category_difference(newPatchesAutomatic, existingPatch.userDecisionSet());
			auto oldUserDecidedWithoutNewOverride = category_difference(oldUserDecided, newPatch.userDecisionSet());
			std::set<Category> newCategories = category_union(newPatchesUserDecided, newAutomaticWithoutExistingOverride);
			std::set<Category> finalResult = category_union(newCategories, oldUserDecidedWithoutNewOverride);
			newPatch.setCategories(finalResult);

			//int64 newPatchUserDecided = bitfield.categorySetAsBitfield(newPatch.categories()) & bitfield.categorySetAsBitfield(newPatch.userDecisionSet());
			//int64 newPatchAutomatic = bitfield.categorySetAsBitfield(newPatch.categories()) & ~bitfield.categorySetAsBitfield(newPatch.userDecisionSet());
			//int64 oldUserDecided = bitfield.categorySetAsBitfield(existingPatch.categories()) & bitfield.categorySetAsBitfield(existingPatch.userDecisionSet());
			//int64 result = newPatchUserDecided | (newPatchAutomatic & ~bitfield.categorySetAsBitfield(existingPatch.userDecisionSet())) | (oldUserDecided & ~bitfield.categorySetAsBitfield(newPatch.userDecisionSet()));

			// User decisions are now a union of both
			std::set<Category> newUserDecisions = category_union(newPatch.userDecisionSet(), existingPatch.userDecisionSet());
			newPatch.setUserDecisions(newUserDecisions);
		}

		int calculateMergedFavorite(PatchHolder const& newPatch, PatchHolder const& existingPatch) {
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
				if (updateChoices & UPDATE_COMMENT) updateClause = prependWithComma(updateClause, "comment = :COM");
				if (updateChoices & UPDATE_AUTHOR) updateClause = prependWithComma(updateClause, "author = :AUT");
				if (updateChoices & UPDATE_INFO) updateClause = prependWithComma(updateClause, "info = :INF");

				try {
					SQLite::Statement sql(db_, "UPDATE patches SET " + updateClause + " WHERE md5 = :MD5 and synth = :SYN");
					if (updateChoices & UPDATE_CATEGORIES) {
						calculateMergedCategories(newPatch, existingPatch);
						sql.bind(":CAT", (int64_t) bitfield.categorySetAsBitfield(newPatch.categories()));
						sql.bind(":CUD", (int64_t) bitfield.categorySetAsBitfield(newPatch.userDecisionSet()));
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
					if (updateChoices & UPDATE_COMMENT) {
						std::string newComment = newPatch.comment();
						if (newComment.empty()) {
							newComment = existingPatch.comment();
						}
						sql.bind(":COM", newComment);
					}
					if (updateChoices & UPDATE_AUTHOR) {
						std::string newAuthor = newPatch.author();
						if (newAuthor.empty()) {
							newAuthor = existingPatch.author();
						}
						sql.bind(":AUT", newAuthor);
					}
					if (updateChoices & UPDATE_INFO) {
						std::string newInfo = newPatch.info();
						if (newInfo.empty()) {
							newInfo = existingPatch.info();
						}
						sql.bind(":INF", newInfo);
					}
					sql.bind(":MD5", newPatch.md5());
					sql.bind(":SYN", existingPatch.synth()->getName());
					if (sql.exec() != 1) {
						jassert(false);
						throw std::runtime_error("FATAL, I don't want to ruin your database");
					}
				}
				catch (SQLite::Exception& ex) {
					spdlog::error("DATABASE ERROR in updatePatch: SQL Exception {}", ex.what());
				}
			}
		}

		bool hasDefaultName(DataFile* patch, std::string const& patchName) {
			auto defaultNameCapa = midikraft::Capability::hasCapability<DefaultNameCapability>(patch);
			if (defaultNameCapa) {
				return defaultNameCapa->isDefaultName(patchName);
			}
			return false;
		}

		bool insertImportInfo(std::string const& synthname, std::string const& source_id, std::string const& importName) {
			// Check if this import already exists 
			try {
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
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in insertImportInfo: SQL Exception {}", ex.what());
			}
			return false;
		}

		size_t mergePatchesIntoDatabase(std::vector<PatchHolder>& patches, std::vector<PatchHolder>& outNewPatches, ProgressHandler* progress, unsigned updateChoice, bool useTransaction) {
			// This works by doing a bulk get operation for the patches from the database...
			auto knownPatches = bulkGetPatches(patches, progress);

			std::unique_ptr<SQLite::Transaction> transaction;
			if (useTransaction) {
				transaction = std::make_unique<SQLite::Transaction>(db_);
			}

			int loop = 0;
			int updatedNames = 0;
			for (auto& patch : patches) {
				if (progress && progress->shouldAbort()) return 0;

				auto md5_key = patch.md5();

				if (knownPatches.find(md5_key) != knownPatches.end()) {
					// Super special logic - do not set the name if the patch name is a default name to prevent us from losing manually given names or those imported from "better" sysex files
					unsigned onlyUpdateThis = updateChoice;
					if (hasDefaultName(patch.patch().get(), patch.name())) {
						onlyUpdateThis = onlyUpdateThis & (~UPDATE_NAME);
					}
					if ((onlyUpdateThis & UPDATE_NAME) && (patch.name() != knownPatches[md5_key].name())) {
						updatedNames++;
						spdlog::info("Renaming {} with better name {}", knownPatches[md5_key].name(), patch.name());
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
				spdlog::info("Updated {} patches in the database with new names", updatedNames);
			}

			// Check if all new patches are editBuffer patches (aka have an invalid MidiBank)
			std::map<std::string, std::string> mapMD5_to_idOfImport;
			std::set<std::tuple<std::string, std::string, std::string>> importsToBeCreated;
			for (const auto& newPatch : outNewPatches) {
				if (!newPatch.sourceInfo()) {
					// Patch with no source info, probably very old or from 3rd party system
				}
				else if (SourceInfo::isEditBufferImport(newPatch.sourceInfo())) {
					// EditBuffer, nothing to do
					// In case this is an EditBuffer import (no bank known), always use the same "fake UUID" "EditBufferImport"
					mapMD5_to_idOfImport[newPatch.md5()] = "EditBufferImport";
					importsToBeCreated.emplace(newPatch.synth()->getName(), "EditBufferImport", "Edit buffer imports");
				}
				else {
					std::string importDisplayString = newPatch.sourceInfo()->toDisplayString(newPatch.synth(), true);;
					std::string importUID = newPatch.sourceInfo()->md5(newPatch.synth());
					if (mapMD5_to_idOfImport.find(newPatch.md5()) == mapMD5_to_idOfImport.end()) {
						// Only use the import ID of the first instance of the patch found, because the loop below will skip all duplicates!
						mapMD5_to_idOfImport[newPatch.md5()] = importUID;
						importsToBeCreated.emplace(newPatch.synth()->getName(), importUID, importDisplayString);
					}
				}
			}

			//TODO can be replaced by repaired bulkPut
			std::map<String, PatchHolder> md5Inserted;
			std::map<Synth*, int> synthsWithUploadedItems;
			int sumOfAll = 0;
			for (const auto& newPatch : outNewPatches) {
				if (progress && progress->shouldAbort()) {
					return (size_t)sumOfAll;
				}
				std::string patchMD5 = newPatch.md5();
				if (md5Inserted.find(patchMD5) != md5Inserted.end()) {
					auto duplicate = md5Inserted[patchMD5];

					// The new one could have better name?
					if (hasDefaultName(duplicate.patch().get(), duplicate.name()) && !hasDefaultName(newPatch.patch().get(), newPatch.name())) {
						updatePatch(newPatch, duplicate, UPDATE_NAME);
						spdlog::info("Updating patch name {} to better one: {}", duplicate.name(), newPatch.name());
					}
					else {
						spdlog::info("Skipping patch {} because it is a duplicate of {}", newPatch.name(),  duplicate.name());
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

		std::pair<int, int> deletePatches(PatchFilter filter) {
			try {
				SQLite::Transaction transaction(db_);
				// Remove patches from non-bank lists.
				std::string remove_from_lists = 
				"DELETE FROM patch_in_list "
					"WHERE ROWID IN ( "
						" SELECT patch_in_list.ROWID FROM patches "
						"   JOIN patch_in_list ON patches.md5 = patch_in_list.md5 AND patches.synth = patch_in_list.synth  "
						"   JOIN lists on lists.id = patch_in_list.id "
						+ buildWhereClause(filter, false) +
					    "   AND lists.synth IS NULL"  // Regular lists have synth NULL, we remove patches we delete from regular lists because they do not destroy banks
					" )";
				SQLite::Statement remove_from_list_query(db_, remove_from_lists.c_str());
				bindWhereClause(remove_from_list_query, filter);
				int items_removed = remove_from_list_query.exec();

				// Patches to hide: those still referenced by a synth bank
				std::string hideStatement =
					"UPDATE patches SET hidden = 1 WHERE ROWID IN ("
					" SELECT patches.ROWID FROM patches "
					" JOIN patch_in_list ON patches.md5 = patch_in_list.md5 AND patches.synth = patch_in_list.synth "
					" JOIN lists ON lists.id = patch_in_list.id "
					"   " + buildWhereClause(filter, false) +
					" AND lists.synth is not NULL "
					")";
				SQLite::Statement hideQuery(db_, hideStatement.c_str());
				bindWhereClause(hideQuery, filter);
				int rowsHidden = hideQuery.exec(); 

				// Patches to delete: those not referenced by any synth bank
				std::string deleteStatement =
					"DELETE FROM patches WHERE ROWID IN ("
					"   SELECT patches.ROWID FROM patches "
					"   " + buildJoinClause(filter, true) + buildWhereClause(filter, false) + " "
					"   AND patch_in_list.id IS NULL"
					")";
				SQLite::Statement deleteQuery(db_, deleteStatement.c_str());
				bindWhereClause(deleteQuery, filter);
				int rowsDeleted = deleteQuery.exec();

				// Step 3: Make sure there are no orphans left in any patch list
				removeAllOrphansFromPatchLists();
				transaction.commit();
				return { rowsDeleted, rowsHidden };
			}
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in deletePatches via filter: SQL Exception {}", ex.what());
			}
			return { 0, 0 };
		}

		std::pair<int, int> deletePatches(std::string const& synth, std::vector<std::string> const& md5s) {
			try {
				int rowsDeleted = 0;
				int rowsHidden = 0;
				for (auto md5 : md5s) {
					// We need to check if the patch to be deleted is part of a bank. Then it cannot be deleted, but just hidden
					// it can be deleted from regular user lists, though, so let's do this first;
					removePatchFromSimpleList(synth, md5);

					if (isPatchPartOfBank(synth, md5)) {
						// Just hide it
						SQLite::Statement query(db_, "UPDATE patches SET hidden = 1 WHERE synth = :SYN and md5 = :MD5");
						query.bind(":SYN", synth);
						query.bind(":MD5", md5);
						rowsHidden += query.exec();
					}
					else {
						// Build a delete query
						std::string deleteStatement = "DELETE FROM patches WHERE md5 = :MD5 AND synth = :SYN";
						SQLite::Statement query(db_, deleteStatement.c_str());
						query.bind(":SYN", synth);
						query.bind(":MD5", md5);
						// Execute
						rowsDeleted += query.exec();
					}
				}

				// Make sure there are no orphans left in any patch list
				//removeAllOrphansFromPatchLists();

				return { rowsDeleted, rowsHidden };
			}
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in deletePatches via md5s: SQL Exception {}", ex.what());
			}
			return{ 0, 0 };
		}

		int reindexPatches(PatchFilter filter) {
			// Give up if more than one synth is selected
			if (filter.synths.size() > 1) {
				spdlog::error("Aborting reindexing - please select only one synth at a time in the advanced filter dialog!");
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

					// First insert the retrieved patches back into the database. The merge logic will handle the multiple instance situation
					std::vector<PatchHolder> remainingPatches;
					mergePatchesIntoDatabase(toBeReinserted, remainingPatches, nullptr, UPDATE_ALL, false);

					// Now, update the patch in list table to point to the newly inserted patch
					for (auto remap : toBeReindexed) {
						try {
							SQLite::Statement query(db_, "SELECT count(*) as num_entries from patch_in_list WHERE synth = :SYN and md5 = :MD5");
							query.bind(":SYN", remap.second.synth()->getName());
							query.bind(":MD5", remap.first);
							bool worked = query.executeStep();
							if (!worked) {
								spdlog::error("Failed to query patch_in_list table, program error");
								return -1;
							}
							int found = query.getColumn("num_entries").getInt();
							if (found > 0) {
								spdlog::error("Found {} list entries for patch, updating {}", found, remap.first);
								SQLite::Statement query(db_, "UPDATE patch_in_list SET md5 = :MDN WHERE synth = :SYN and md5 = :MD5");
								query.bind(":SYN", remap.second.synth()->getName());
								query.bind(":MD5", remap.first);
								query.bind(":MDN", remap.second.md5());
								int rowUpdated = query.exec();
								if (rowUpdated != found) {
									spdlog::error("Aborting reindexing - could not update patch in list entry for md5 {}: {} updated but {} expected", remap.first, rowUpdated, found);
									return -1;
								}
							}
						}
						catch (SQLite::Exception& e) {
							spdlog::error("Database error when reindexing patches: {}", e.what());
							return -1;
						}
					}

					// We got everything into the RAM - do we dare do delete them from the database now?
					auto [deleted, hidden] = deletePatches(filter.synths.begin()->second.lock()->getName(), toBeDeleted);
					if (deleted != (int)toBeReindexed.size()) {
						spdlog::error("Aborting reindexing - count of deleted patches does not match count of retrieved patches. Program Error.");
						return -1;
					}

					transaction.commit();

					return getPatchesCount(filter);
				}
				else {
					spdlog::info("None of the selected patches needed reindexing skipping!");
					return getPatchesCount(filter);
				}
			}
			else {
				spdlog::error("Aborting reindexing - database error retrieving the filtered patches");
				return -1;
			}

		}

		std::string databaseFileName() const
		{
			return db_.getFilename();
		}

		std::shared_ptr<AutomaticCategory> getCategorizer() {
			ScopedLock lock(categoryLock_);
			// Force reload of the categories from the database table
			categoryDefinitions_ = getCategories();
			int bitindex = bitfield.maxBitIndex();

			// The Categorizer currently is constructed from two sources - the list of categories in the database including the bit index
			// The auto-detection rules are stored in the jsonc file.
			// This needs to be merged.
			auto categorizer = std::make_shared<AutomaticCategory>(categoryDefinitions_);

			// First pass - check that all categories referenced in the auto category file are stored in the database, else they will have no bit index!
			SQLite::Transaction transaction(db_);
			for (auto rule : categorizer->loadedRules()) {
				auto exists = false;
				for (auto cat : categoryDefinitions_) {
					if (cat.category() == rule.category().category()) {
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
						sql.bind(":NAM", rule.category().category());
						sql.bind(":COL", rule.category().color().toDisplayString(true).toStdString());
						sql.exec();
					}
					else {
						jassert(false);
						spdlog::error("FATAL ERROR - Can only deal with 64 different categories. Please remove some categories from the rules file!");
						return categorizer;
					}
				}
			}
			transaction.commit();

			// Refresh from database
			categoryDefinitions_ = getCategories();

			// Now we need to merge the database persisted categories with the ones defined in the automatic categories from the json string
			for (auto cat : categoryDefinitions_) {
				bool exists = false;
				for (auto rule : categorizer->loadedRules()) {
					if (cat.category() == rule.category().category()) {
						// Copy the rules
						exists = true;
						categorizer->addAutoCategory(AutoCategoryRule(rule.category(), rule.patchNameMatchers()));
						break;
					}
				}
				if (!exists) {
					// That just means there are no rules, but it needs to be added to the list of available categories anyway
					categorizer->addAutoCategory(AutoCategoryRule(Category(cat), std::vector<std::string>()));
				}
			}

			return categorizer;
		}

		std::vector<ListInfo> allSynthBanks(std::shared_ptr<Synth> synth)
		{
			try {
				SQLite::Statement query(db_, "SELECT * FROM lists WHERE synth = :SYN AND midi_bank_number is not NULL");
				query.bind(":SYN", synth->getName());
				std::vector<ListInfo> result;
				while (query.executeStep()) {
					std::string bankId(query.getColumn("id").getText());
					//TODO This is a hack, if the ID starts with the synth name it is an active SynthBank...
					if (bankId.find(synth->getName()) == 0) {
						result.push_back({ bankId, query.getColumn("name").getText() });
					}
				}
				return result;
			}
			catch (SQLite::Exception& e) {
				spdlog::error("Database error when retrieving lists of user banks: {}", e.what());
				return {};
			}
		}

		std::vector<ListInfo> allUserBanks(std::shared_ptr<Synth> synth)
		{
			try {
				SQLite::Statement query(db_, "SELECT * FROM lists WHERE synth = :SYN AND midi_bank_number is not NULL");
				query.bind(":SYN", synth->getName());
				std::vector<ListInfo> result;
				while (query.executeStep()) {
					std::string bankId(query.getColumn("id").getText());
					//TODO This is a hack, if the ID starts with the synth name it is an active SynthBank...
					if (bankId.find(synth->getName()) != 0) {
						result.push_back({ bankId, query.getColumn("name").getText() });
					}
				}
				return result;
			}
			catch (SQLite::Exception& e) {
				spdlog::error("Database error when retrieving lists of user banks: {}", e.what());
				return {};
			}
		}

		std::vector<ListInfo> allPatchLists()
		{
			try {
				SQLite::Statement query(db_, "SELECT * FROM lists WHERE synth is null");
				std::vector<ListInfo> result;
				while (query.executeStep()) {
					result.push_back({ query.getColumn("id").getText(), query.getColumn("name").getText() });
				}
				return result;
			}
			catch (SQLite::Exception& e) {
				spdlog::error("Database error when retrieving lists of patches: {}", e.what());
				return {};
			}
		}

		bool doesListExist(std::string listId) {
			SQLite::Statement query(db_, "SELECT count(*) as num_lists FROM lists WHERE id = :ID");
			query.bind(":ID", listId);
			if (query.executeStep()) {
				auto result = query.getColumn("num_lists");
				return result.getInt() != 0;
			}
			return false;
		}

		std::shared_ptr<midikraft::PatchList> getPatchList(ListInfo info, std::map<std::string, std::weak_ptr<Synth>> synths)
		{
			// First load the list
			SQLite::Statement queryList(db_, "SELECT * FROM lists WHERE id = :ID");
			queryList.bind(":ID", info.id);
			std::shared_ptr<midikraft::PatchList> list;
			if (queryList.executeStep()) {
				if (queryList.getColumn("synth").isNull()) {
					list = std::make_shared<midikraft::PatchList>(info.id, queryList.getColumn("name").getText());
				}
				else {
					// Find synth
					auto synthName = queryList.getColumn("synth").getText();
					for (auto synth : synths) {
						auto s = synth.second.lock();
						if (s->getName() == synthName) {
							int bankInt = queryList.getColumn("midi_bank_number").getInt();
							if (info.id.find(synthName) != 0) {
								// This is a stored user bank
								list = std::make_shared<UserBank>(info.id, queryList.getColumn("name").getText(),
									s
									, MidiBankNumber::fromZeroBase(bankInt, SynthBank::numberOfPatchesInBank(s, bankInt))
									);
							}
							else {
								// This is an active bank
								list = std::make_shared<ActiveSynthBank>(s
									, MidiBankNumber::fromZeroBase(bankInt, SynthBank::numberOfPatchesInBank(s, bankInt))
									, juce::Time(queryList.getColumn("last_synced").getInt64())
									);
							}
							break;
						}
					}
					if (!list) {
						spdlog::error("Can't load list of synth that is not configured!");
						return nullptr;
					}
				}
			}
			if (!list) {
				spdlog::error("Failed to create list!");
				return nullptr;
			}

			// Now load the patches in this list
			SQLite::Statement query(db_, "SELECT * from patch_in_list where id=:ID order by order_num");
			query.bind(":ID", info.id.c_str());
			std::vector<std::pair<std::string, std::string>> md5s;
			while (query.executeStep()) {
				md5s.push_back({ query.getColumn("synth").getText(), query.getColumn("md5").getText() });
			}
			std::vector<PatchHolder> result;
			for (auto const& md5 : md5s) {
				if (synths.find(md5.first) != synths.end()) {
					getSinglePatch(synths[md5.first].lock(), md5.second, result);
				}
			}
			list->setPatches(result);
			return list;
		}

		void addPatchToListInternal(std::string const& listId, std::string const& synthName, std::string const& md5, int insertIndex) {
			SQLite::Statement insert(db_, "INSERT INTO patch_in_list (id, synth, md5, order_num) VALUES (:ID, :SYN, :MD5, :ONO)");
			insert.bind(":ID", listId);
			insert.bind(":SYN", synthName);
			insert.bind(":MD5", md5);
			insert.bind(":ONO", insertIndex);
			insert.exec();
		}

		void addPatchToList(ListInfo info, PatchHolder const& patch, int insertIndex) {
			try {
				SQLite::Transaction transaction(db_);
				// First make room by moving existing items up
				SQLite::Statement update(db_, "UPDATE patch_in_list SET order_num = order_num + 1 WHERE id = :ID AND order_num >= :ONO");
				update.bind(":ID", info.id);
				update.bind(":ONO", insertIndex);
				update.exec();
				addPatchToListInternal(info.id, patch.synth()->getName(), patch.md5(), insertIndex);
				transaction.commit();
			}
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in addPatchToList: SQL Exception {}", ex.what());
			}
		}

		void renumList(std::string const& list_id) {
			// Call this within a transaction!
			SQLite::Statement renum(db_, "WITH po AS (SELECT*, ROW_NUMBER() OVER(order by order_num) - 1 AS new_order FROM patch_in_list WHERE id = :ID) "
				"UPDATE patch_in_list AS pl SET order_num = (SELECT new_order FROM po WHERE pl.synth = po.synth AND pl.md5 = po.md5 AND pl.order_num = po.order_num) where id = :ID");
			renum.bind(":ID", list_id);
			renum.exec();
		}

		void movePatchInList(ListInfo info, PatchHolder const& patch, int previousIndex, int newIndex) {
			try {
				SQLite::Transaction transaction(db_);
				// First make room by moving existing items up
				SQLite::Statement update(db_, "UPDATE patch_in_list SET order_num = order_num + 1 WHERE id = :ID AND order_num >= :ONO");
				update.bind(":ID", info.id);
				update.bind(":ONO", newIndex);
				update.exec();
				// Now update the existing element at the previous index at put it at the new Index
				SQLite::Statement update2(db_, "UPDATE patch_in_list SET order_num = :ONO WHERE id = :ID AND synth = :SYN AND md5 = :MD5 AND order_num = :INC");
				update2.bind(":ID", info.id);
				update2.bind(":SYN", patch.smartSynth()->getName());
				update2.bind(":MD5", patch.md5());
				update2.bind(":INC", newIndex > previousIndex ? previousIndex : previousIndex + 1);
				update2.bind(":ONO", newIndex);
				update2.exec();
				// Then we may have created a gap in the list, so just renum the whole list
				renumList(info.id);
				transaction.commit();
			}
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in addPatchToList: SQL Exception {}", ex.what());
			}
		}

		void removePatchFromList(std::string const& list_id, std::string const& synth_name, std::string const& md5, int order_num) {
			try {
				SQLite::Transaction transaction(db_);
				SQLite::Statement removeIt(db_, "DELETE FROM patch_in_list WHERE id = :ID AND synth = :SYN AND md5 = :MD5 AND order_num = :ONO");
				removeIt.bind(":ID", list_id);
				removeIt.bind(":SYN", synth_name);
				removeIt.bind(":MD5", md5);
				removeIt.bind(":ONO", order_num);
				removeIt.exec();
				renumList(list_id);
				transaction.commit();
			}
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in removePatchFromList: SQL Exception {}", ex.what());
			}
		}

		void putPatchList(std::shared_ptr<PatchList> patchList)
		{
			try {
				// Check if it exists
				SQLite::Transaction transaction(db_);
				SQLite::Statement search(db_, "SELECT * FROM lists WHERE id = :ID");
				search.bind(":ID", patchList->id());
				auto isSynthBank = std::dynamic_pointer_cast<SynthBank>(patchList);
				if (search.executeStep()) {
					if (!isSynthBank) {
						SQLite::Statement update(db_, "UPDATE lists SET name = :NAM WHERE id = :ID");
						update.bind(":ID", patchList->id());
						update.bind(":NAM", patchList->name());
						update.exec();
					}
					else {
						SQLite::Statement update(db_, "UPDATE lists SET name = :NAM, last_synced = :LSY WHERE id = :ID");
						update.bind(":ID", patchList->id());
						update.bind(":NAM", patchList->name());
						if (auto activeBank = std::dynamic_pointer_cast<midikraft::ActiveSynthBank>(patchList)) {
							update.bind(":LSY", (int64_t) activeBank->lastSynced().toMilliseconds());
						}
						else {
							update.bind(":LSY", 0);
						}
						update.exec();
					}
					// Delete the previous list content, this operation overwrites the list!
					SQLite::Statement removeEntries(db_, "DELETE FROM patch_in_list WHERE id = :ID");
					removeEntries.bind(":ID", patchList->id());
					removeEntries.exec();
				}
				else {
					SQLite::Statement insert(db_, "INSERT INTO lists (id, name, synth, midi_bank_number, last_synced) VALUES (:ID, :NAM, :SYN, :BNK, :LSY)");
					insert.bind(":ID", patchList->id());
					insert.bind(":NAM", patchList->name());
					if (isSynthBank) {
						insert.bind(":SYN", isSynthBank->synth()->getName());
						insert.bind(":BNK", isSynthBank->bankNumber().toZeroBased());
						if (auto activeBank = std::dynamic_pointer_cast<midikraft::ActiveSynthBank>(patchList)) {
							insert.bind(":LSY", (int64_t) activeBank->lastSynced().toMilliseconds()); // Storing UNIX epoch here
						}
						else {
							insert.bind(":LSY", 0);
						}
					}
					else {
						// Insert NULLs for those three columns
						insert.bind(":SYN");
						insert.bind(":BNK");
						insert.bind(":LSY");
					}
					insert.exec();
				}
				// If this list already has a list of patches, make sure to add them into the patch list as well!
				int i = 0;
				for (auto patch : patchList->patches()) {
					addPatchToListInternal(patchList->id(), patch.synth()->getName(), patch.md5(), i++);
				}

				transaction.commit();
			}
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in putPatchList: SQL Exception {}", ex.what());
			}
		}

		void deletePatchlist(ListInfo info) {
			try {
				SQLite::Statement deleteMembers(db_, "DELETE FROM patch_in_list WHERE id = :ID");
				deleteMembers.bind(":ID", info.id);
				deleteMembers.exec();
				SQLite::Statement deleteIt(db_, "DELETE FROM lists WHERE id = :ID");
				deleteIt.bind(":ID", info.id);
				deleteIt.exec();
			}
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in deletePatchlist: SQL Exception {}", ex.what());
			}
		}

		void removePatchFromSimpleList(std::string const& synth, std::string const& md5) {
			// Simple List as in - this is not a bank. It is not allowed to delete patches that are part of a bank
			SQLite::Statement removeFromSimpleList(db_, "DELETE FROM patch_in_list WHERE synth = :SYN AND md5 = :MD5 AND EXISTS (SELECT * FROM lists WHERE id = patch_in_list.id AND synth IS NULL)");
			removeFromSimpleList.bind(":SYN", synth);
			removeFromSimpleList.bind(":MD5", md5);
			removeFromSimpleList.exec();
		}

		bool isPatchPartOfBank(std::string const& synth, std::string const& md5) {
			SQLite::Statement findBankForPatch(db_, "SELECT COUNT(*) FROM lists INNER JOIN patch_in_list AS pil ON lists.id = pil.id WHERE lists.synth = :SYN AND pil.md5 = :MD5");
			findBankForPatch.bind(":SYN", synth);
			findBankForPatch.bind(":MD5", md5);
			if (findBankForPatch.executeStep()) {
				return findBankForPatch.getColumn(0).getInt() > 0;
			}
			spdlog::error("Program error determining if patch is part of bank, hoping for the best from here...");
			return false;
		}

		void removeAllOrphansFromPatchLists() {
			try {
				SQLite::Statement cleanupPatchLists(db_,
					"delete from patch_in_list as pil where not exists(select * from patches as p where p.md5 = pil.md5 and p.synth = pil.synth)");
				cleanupPatchLists.exec();
			}
			catch (SQLite::Exception& ex) {
				spdlog::error("DATABASE ERROR in removeAllOrphansFromPatchLists: SQL Exception {}", ex.what());
			}
		}


	private:
		SQLite::Database db_;
		OpenMode mode_;
		CategoryBitfield bitfield;
		std::vector<Category> categoryDefinitions_;
		CriticalSection categoryLock_;
	};

	PatchDatabase::PatchDatabase(bool overwrite) {
		try {
			File location(generateDefaultDatabaseLocation());
			if (location.exists() && !overwrite) {
				location = location.getNonexistentSibling();
			}
			impl.reset(new PatchDataBaseImpl(location.getFullPathName().toStdString(), OpenMode::READ_WRITE));
		}
		catch (SQLite::Exception& e) {
			throw PatchDatabaseException(e.what());
		}
	}

	PatchDatabase::PatchDatabase(std::string const& databaseFile, OpenMode mode) {
		try {
			impl.reset(new PatchDataBaseImpl(databaseFile, mode));
		}
		catch (SQLite::Exception& e) {
			if (e.getErrorCode() == SQLITE_READONLY) {
				throw PatchDatabaseReadonlyException(e.what());
			}
			else {
				throw PatchDatabaseException(e.what());
			}
		}
	}

	PatchDatabase::~PatchDatabase() {
	}

	std::string PatchDatabase::getCurrentDatabaseFileName() const
	{
		return impl->databaseFileName();
	}

	bool PatchDatabase::switchDatabaseFile(std::string const& newDatabaseFile, OpenMode mode)
	{
		try {
			auto newDatabase = new PatchDataBaseImpl(newDatabaseFile, mode);
			// If no exception was thrown, this worked
			impl.reset(newDatabase);
			return true;
		}
		catch (SQLite::Exception& ex) {
			spdlog::error("Failed to open database: {}", ex.what());
		}
		return false;
	}

	int PatchDatabase::getPatchesCount(PatchFilter filter)
	{
		return impl->getPatchesCount(filter);
	}

	bool PatchDatabase::getSinglePatch(std::shared_ptr<Synth> synth, std::string const& md5, std::vector<PatchHolder>& result)
	{
		return impl->getSinglePatch(synth, md5, result);
	}

	std::vector<MidiProgramNumber> PatchDatabase::getBankPositions(std::shared_ptr<Synth> synth, std::string const& md5) {
		return impl->getBankPositions(synth, md5);
	}

	bool PatchDatabase::putPatch(PatchHolder const& patch) {
		// From the logic, this is an UPSERT (REST call put)
		// Use the merge functionality for this!
		std::vector<PatchHolder> newPatches;
		newPatches.push_back(patch);
		std::vector<PatchHolder> insertedPatches;
		return impl->mergePatchesIntoDatabase(newPatches, insertedPatches, nullptr, UPDATE_ALL, true);
	}

	bool PatchDatabase::putPatches(std::vector<PatchHolder> const& patches) {
		ignoreUnused(patches);
		jassert(false);
		return false;
	}

	std::shared_ptr<AutomaticCategory> PatchDatabase::getCategorizer()
	{
		return impl->getCategorizer();
	}

	int PatchDatabase::getNextBitindex() {
		return impl->getNextBitindex();
	}

	void PatchDatabase::updateCategories(std::vector<CategoryDefinition> const& newdefs)
	{
		impl->updateCategories(newdefs);
	}

	std::vector<ListInfo> PatchDatabase::allPatchLists()
	{
		return impl->allPatchLists();
	}

	std::vector<ListInfo> PatchDatabase::allSynthBanks(std::shared_ptr<Synth> synth)
	{
		return impl->allSynthBanks(synth);
	}

	std::vector<ListInfo> PatchDatabase::allUserBanks(std::shared_ptr<Synth> synth)
	{
		return impl->allUserBanks(synth);
	}

	bool PatchDatabase::doesListExist(std::string listId) {
		return impl->doesListExist(listId);
	}

	std::shared_ptr<midikraft::PatchList> PatchDatabase::getPatchList(ListInfo info, std::map<std::string, std::weak_ptr<Synth>> synths)
	{
		return impl->getPatchList(info, synths);
	}

	void PatchDatabase::putPatchList(std::shared_ptr<PatchList> patchList)
	{
		impl->putPatchList(patchList);
	}

	void PatchDatabase::deletePatchlist(ListInfo info)
	{
		impl->deletePatchlist(info);
	}

	void PatchDatabase::addPatchToList(ListInfo info, PatchHolder const& patch, int insertIndex)
	{
		impl->addPatchToList(info, patch, insertIndex);
	}

	void PatchDatabase::movePatchInList(ListInfo info, PatchHolder const& patch, int previousIndex, int newIndex)
	{
		impl->movePatchInList(info, patch, previousIndex, newIndex);
	}

	void PatchDatabase::removePatchFromList(std::string const& list_id, std::string const& synth_name, std::string const& md5, int order_num)
	{
		impl->removePatchFromList(list_id, synth_name, md5, order_num);
	}

	std::pair<int, int> PatchDatabase::deletePatches(PatchFilter filter)
	{
		return impl->deletePatches(filter);
	}

	std::pair<int, int> PatchDatabase::deletePatches(std::string const& synth, std::vector<std::string> const& md5s)
	{
		return impl->deletePatches(synth, md5s);
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
				spdlog::warn("Found {} patches with inconsistent MD5 - please run the Edit... Reindex Patches command for this synth", faultyIndexedPatches.size());
			}
			return result;
		}
		else {
			return {};
		}
	}

	void PatchDatabase::getPatchesAsync(PatchFilter filter, std::function<void(PatchFilter const filteredBy, std::vector<PatchHolder> const&)> finished, int skip, int limit)
	{
		pool_.addJob([this, filter, finished, skip, limit]() {
			auto result = getPatches(filter, skip, limit);
			MessageManager::callAsync([filter, finished, result]() {
				finished(filter, result);
				});
			});
	}

	size_t PatchDatabase::mergePatchesIntoDatabase(std::vector<PatchHolder>& patches, std::vector<PatchHolder>& outNewPatches, ProgressHandler* progress, unsigned updateChoice)
	{
		return impl->mergePatchesIntoDatabase(patches, outNewPatches, progress, updateChoice, true);
	}

	std::vector<ImportInfo> PatchDatabase::getImportsList(Synth* activeSynth) const {
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

	std::string PatchDatabase::makeDatabaseBackup(std::string const& suffix) {
		return impl->makeDatabaseBackup(suffix);
	}

	void PatchDatabase::makeDatabaseBackup(File backupFileToCreate)
	{
		impl->makeDatabaseBackup(backupFileToCreate);
	}

	void PatchDatabase::makeDatabaseBackup(File databaseFile, File backupFileToCreate)
	{
		PatchDataBaseImpl::makeDatabaseBackup(databaseFile, backupFileToCreate);
	}

	bool PatchDatabase::renameImport(std::string synthName, std::string importID, std::string newName) {
		return impl->renameImport(synthName, importID, newName);
	}

	std::vector<Category> PatchDatabase::getCategories() const {
		return impl->getCategories();
	}
}

