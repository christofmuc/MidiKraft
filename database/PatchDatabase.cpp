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

#include "SQLiteCpp/../../sqlite3/sqlite3.h" //TODO How to use the underlying site3 correctly?

namespace midikraft {

	const std::string kDataBaseFileName = "SysexDatabaseOfAllPatches.db3";
	const std::string kDataBaseBackupSuffix = "-backup";

	const int SCHEMA_VERSION = 6;
	/* History */
	/* 1 - Initial schema */
	/* 2 - adding hidden flag (aka deleted) */
	/* 3 - adding type integer to patch (to differentiate voice, patch, layer, tuning...) */
	/* 4 - forgot to migrate existing data NULL to 0 */
	/* 5 - adding bank number column for better sorting of multi-imports */
	/* 6 - adding the table categories to track which bit index is used for which tag */
	/* 7 - adding the table lists to allow storing lists of patches */

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

		void createSchema() {
			if (false)
			{
				SQLite::Transaction transaction(db_);
				db_.exec("DROP TABLE IF EXISTS patches");
				db_.exec("DROP TABLE IF EXISTS imports");
				db_.exec("DROP TABLE IF EXISTS schema_version");
				transaction.commit();
			}

			SQLite::Transaction transaction(db_);
			if (!db_.tableExists("patches")) {
				db_.exec("CREATE TABLE IF NOT EXISTS patches (synth TEXT, md5 TEXT UNIQUE, name TEXT, type INTEGER, data BLOB, favorite INTEGER, hidden INTEGER, sourceID TEXT, sourceName TEXT,"
					" sourceInfo TEXT, midiBankNo INTEGER, midiProgramNo INTEGER, categories INTEGER, categoryUserDecision INTEGER)");
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
				db_.exec("CREATE TABLE IF NOT EXISTS lists(id TEXT UNIQUE NOT NULL, name TEXT)");
				db_.exec("CREATE TABLE IF NOT EXISTS patch_in_list(id TEXT, synth TEXT, md5 TEXT, order_num INTEGER NOT NULL, FOREIGN KEY(id) REFERENCES lists(id))");
			}

			// Commit transaction
			transaction.commit();

			// Check if schema needs to be migrated
			SQLite::Statement schemaQuery(db_, "SELECT number FROM schema_version");
			if (schemaQuery.executeStep()) {
				int version = schemaQuery.getColumn("number").getInt();
				if (version < SCHEMA_VERSION) {
					try {
						migrateSchema(version);
					}
					catch (SQLite::Exception& e) {
						if (mode_ == OpenMode::READ_WRITE) {
							std::string message = (boost::format("Cannot open database file %s - Cannot upgrade to latest version, schema version found is %d. Error: %s") % db_.getFilename() % version % e.what()).str();
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
					std::string message = (boost::format("Cannot open database file %s - this was produced with a newer version of KnobKraft Orm, schema version is %d.") % db_.getFilename() % version).str();
					if (mode_ == OpenMode::READ_WRITE) {
						AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Database Error", message);
					}
					throw new SQLite::Exception(message);
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
				sql.bind(":BNK", patch.bankNumber().isValid() ? patch.bankNumber().toZeroBased() : 0);
				sql.bind(":PRG", patch.patchNumber().toZeroBased());
				sql.bind(":CAT", bitfield.categorySetAsBitfield(patch.categories()));
				sql.bind(":CUD", bitfield.categorySetAsBitfield(patch.userDecisionSet()));

				sql.exec();
			}
			catch (SQLite::Exception& ex) {
				SimpleLogger::instance()->postMessage((boost::format("DATABASE ERROR in putPatch: SQL Exception %s") % ex.what()).str());
			}
			return true;
		}

		std::vector<ImportInfo> getImportsList(Synth* activeSynth) {
			SQLite::Statement query(db_, "SELECT imports.name, id, count(patches.md5) AS patchCount FROM imports JOIN patches on imports.id == patches.sourceID WHERE patches.synth = :SYN AND imports.synth = :SYN GROUP BY imports.id ORDER BY date");
			query.bind(":SYN", activeSynth->getName());
			std::vector<ImportInfo> result;
			while (query.executeStep()) {
				std::string description = (boost::format("%s (%d)") % query.getColumn("name").getText() % query.getColumn("patchCount").getInt()).str();
				result.push_back({ query.getColumn("name").getText(), description, query.getColumn("id").getText() });
			}
			return result;
		}

		std::string buildWhereClause(PatchFilter filter, bool needsCollate) {
			std::string where = " WHERE 1 == 1 ";
			if (!filter.synths.empty()) {
				//TODO does SQlite do "IN" clause?
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
				if (needsCollate) {
					where += " COLLATE NOCASE";
				}
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

		void bindWhereClause(SQLite::Statement& query, PatchFilter filter) {
			int s = 0;
			for (auto const& synth : filter.synths) {
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
			if (!filter.onlyUntagged && !filter.categories.empty()) {
				query.bind(":CAT", bitfield.categorySetAsBitfield(filter.categories));
			}
		}

		int getPatchesCount(PatchFilter filter) {
			try {
				SQLite::Statement query(db_, "SELECT count(*) FROM patches" + buildWhereClause(filter, false));
				bindWhereClause(query, filter);
				if (query.executeStep()) {
					return query.getColumn(0).getInt();
				}
			}
			catch (SQLite::Exception& ex) {
				SimpleLogger::instance()->postMessage((boost::format("DATABASE ERROR in getPatchesCount: SQL Exception %s") % ex.what()).str());
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
					SimpleLogger::instance()->postMessage("You have exhausted the 63 possible categories, it is no longer possible to create new ones in this database. Consider splitting the database via PatchInterchangeFormat files");
					return -1;
				}
			}
			SimpleLogger::instance()->postMessage("Unexpected program error determining the next bit index!");
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
				SimpleLogger::instance()->postMessage((boost::format("DATABASE ERROR in updateCategories: SQL Exception %s") % ex.what()).str());
			}
		}

		bool loadPatchFromQueryRow(std::shared_ptr<Synth> synth, SQLite::Statement& query, std::vector<PatchHolder>& result) {
			ScopedLock lock(categoryLock_);

			std::shared_ptr<DataFile> newPatch;

			// Create the patch itself, from the BLOB stored
			auto dataColumn = query.getColumn("data");

			if (dataColumn.isBlob()) {
				std::vector<uint8> patchData((uint8*)dataColumn.getBlob(), ((uint8*)dataColumn.getBlob()) + dataColumn.getBytes());
				//TODO I should not need the midiProgramNumber here
				int midiProgramNumber = query.getColumn("midiProgramNo").getInt();
				newPatch = synth->patchFromPatchData(patchData, MidiProgramNumber::fromZeroBase(midiProgramNumber));
			}
			// We need the current categories
			categoryDefinitions_ = getCategories();
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
					std::set<Category> updateSet;
					bitfield.makeSetOfCategoriesFromBitfield(updateSet, query.getColumn("categories").getInt64());
					holder.setCategories(updateSet);
					bitfield.makeSetOfCategoriesFromBitfield(updateSet, query.getColumn("categoryUserDecision").getInt64());
					holder.setUserDecisions(updateSet);

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
				SimpleLogger::instance()->postMessage((boost::format("DATABASE ERROR in getSinglePatch: SQL Exception %s") % ex.what()).str());
			}
			return false;
		}

		bool getPatches(PatchFilter filter, std::vector<PatchHolder>& result, std::vector<std::pair<std::string, PatchHolder>>& needsReindexing, int skip, int limit) {
			std::string selectStatement = "SELECT * FROM patches " + buildWhereClause(filter, true) + " ORDER BY sourceID, midiBankNo, midiProgramNo ";
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
			catch (SQLite::Exception& ex) {
				SimpleLogger::instance()->postMessage((boost::format("DATABASE ERROR in getPatches: SQL Exception %s") % ex.what()).str());
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
						int midiBankNumber = query.getColumn("midiBankNo").getInt();
						int midiProgramNumber = query.getColumn("midiProgramNo").getInt();
						PatchHolder existingPatch(ph.smartSynth(), ph.sourceInfo(), nullptr, MidiBankNumber::fromZeroBase(midiBankNumber), MidiProgramNumber::fromZeroBase(midiProgramNumber));
						std::string name = query.getColumn("name");
						existingPatch.setName(name);
						result.emplace(md5, existingPatch);
					}
				}
				catch (SQLite::Exception& ex) {
					SimpleLogger::instance()->postMessage((boost::format("DATABASE ERROR in bulkGetPatches: SQL Exception %s") % ex.what()).str());
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

				try {
					SQLite::Statement sql(db_, "UPDATE patches SET " + updateClause + " WHERE md5 = :MD5 and synth = :SYN");
					if (updateChoices & UPDATE_CATEGORIES) {
						calculateMergedCategories(newPatch, existingPatch);
						sql.bind(":CAT", bitfield.categorySetAsBitfield(newPatch.categories()));
						sql.bind(":CUD", bitfield.categorySetAsBitfield(newPatch.userDecisionSet()));
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
				catch (SQLite::Exception& ex) {
					SimpleLogger::instance()->postMessage((boost::format("DATABASE ERROR in updatePatch: SQL Exception %s") % ex.what()).str());
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
				SimpleLogger::instance()->postMessage((boost::format("DATABASE ERROR in insertImportInfo: SQL Exception %s") % ex.what()).str());
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
						SimpleLogger::instance()->postMessage((boost::format("Renaming %s with better name %s") % knownPatches[md5_key].name() % patch.name()).str());
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
					mapMD5_to_idOfImport[newPatch.md5()] = importUID;
					importsToBeCreated.emplace(newPatch.synth()->getName(), importUID, importDisplayString);
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
			try {
				// Build a delete query
				std::string deleteStatement = "DELETE FROM patches " + buildWhereClause(filter, false);
				SQLite::Statement query(db_, deleteStatement.c_str());
				bindWhereClause(query, filter);

				// Execute
				int rowsDeleted = query.exec();
				return rowsDeleted;
			}
			catch (SQLite::Exception& ex) {
				SimpleLogger::instance()->postMessage((boost::format("DATABASE ERROR in deletePatches via filter: SQL Exception %s") % ex.what()).str());
			}
			return 0;
		}

		int deletePatches(std::string const& synth, std::vector<std::string> const& md5s) {
			try {
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
			catch (SQLite::Exception& ex) {
				SimpleLogger::instance()->postMessage((boost::format("DATABASE ERROR in deletePatches via md5s: SQL Exception %s") % ex.what()).str());
			}
			return 0;
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
					if (deleted != (int)toBeReindexed.size()) {
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
						SimpleLogger::instance()->postMessage("FATAL ERROR - Can only deal with 64 different categories. Please remove some categories from the rules file!");
						return categorizer;
					}
				}
			}
			transaction.commit();

			// Refresh from database
			categoryDefinitions_ = getCategories();

			// Now we need to merge the database persisted categories with the ones defined in the automatic categories from the json string
			bool exists = false;
			for (auto cat : categoryDefinitions_) {
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

		std::vector<ListInfo> allPatchLists()
		{
			SQLite::Statement query(db_, "SELECT * from lists");
			std::vector<ListInfo> result;
			while (query.executeStep()) {
				result.push_back({ query.getColumn("id").getText(), query.getColumn("name").getText() });
			}
			return result;
		}

		midikraft::PatchList getPatchList(ListInfo info, std::map<std::string, std::weak_ptr<Synth>> synths)
		{
			PatchList list(info.id, info.name);
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
			list.setPatches(result);
			return list;
		}

		void addPatchToList(ListInfo info, PatchHolder const& patch) {
			try {
				SQLite::Statement insert(db_, "INSERT INTO patch_in_list (id, synth, md5, order_num) VALUES (:ID, :SYN, :MD5, :ONO)");
				insert.bind(":ID", info.id);
				insert.bind(":SYN", patch.smartSynth()->getName());
				insert.bind(":MD5", patch.md5());
				insert.bind(":ONO", 0);
				insert.exec();
			}
			catch (SQLite::Exception& ex) {
				SimpleLogger::instance()->postMessage((boost::format("DATABASE ERROR in addPatchToList: SQL Exception %s") % ex.what()).str());
			}
		}

		void putPatchList(PatchList patchList)
		{
		}


	private:
		SQLite::Database db_;
		OpenMode mode_;
		CategoryBitfield bitfield;
		std::vector<Category> categoryDefinitions_;
		CriticalSection categoryLock_;
	};

	PatchDatabase::PatchDatabase() {
		try {
			impl.reset(new PatchDataBaseImpl(generateDefaultDatabaseLocation(), OpenMode::READ_WRITE));
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
			SimpleLogger::instance()->postMessage("Failed to open database: " + String(ex.what()));
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

	midikraft::PatchList PatchDatabase::getPatchList(ListInfo info, std::map<std::string, std::weak_ptr<Synth>> synths)
	{
		return impl->getPatchList(info, synths);
	}

	void PatchDatabase::putPatchList(PatchList patchList)
	{
		impl->putPatchList(patchList);
	}

	void PatchDatabase::addPatchToList(ListInfo info, PatchHolder const& patch)
	{
		impl->addPatchToList(info, patch);
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

	std::vector<Category> PatchDatabase::getCategories() const {
		return impl->getCategories();
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

	midikraft::PatchDatabase::PatchFilter PatchDatabase::allPatchesFilter(std::vector<std::shared_ptr<Synth>> synths)
	{
		PatchFilter filter;
		filter.onlyFaves = false;
		filter.onlySpecifcType = false;
		filter.onlyUntagged = false;
		filter.showHidden = true;
		for (auto const& synth : synths) {
			filter.synths.emplace(synth->getName(), synth);
		}
		return filter;
	}

	bool operator!=(PatchDatabase::PatchFilter const& a, PatchDatabase::PatchFilter const& b)
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
			|| a.onlyFaves != b.onlyFaves
			|| a.onlySpecifcType != b.onlySpecifcType
			|| a.typeID != b.typeID
			|| a.showHidden != b.showHidden
			|| a.onlyUntagged != b.onlyUntagged;
	}

}
