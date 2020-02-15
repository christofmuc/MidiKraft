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

	// Define SQL database schema
	struct SQL_Patch {
		//std::string user;
		std::string synth;
		std::vector<uint8> data;
		int favorite;
		std::string sourceInfo;
		std::set<std::string> category;
	};

	class PatchDatabase::PatchDataBaseImpl {
	public:
		PatchDatabase::PatchDataBaseImpl() : db_("patches.db3", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE) {
			createSchema();
		}

		void createSchema() {
			if (false)
			{
				SQLite::Transaction transaction(db_);
				db_.exec("DROP TABLE patches");
				db_.exec("DROP TABLE imports");
				transaction.commit();
			}

			if (!db_.tableExists("patches") || !db_.tableExists("imports")) {
				SQLite::Transaction transaction(db_);
				db_.exec("CREATE TABLE IF NOT EXISTS patches (synth TEXT, md5 TEXT, name TEXT, data BLOB, favorite INTEGER, sourceID TEXT, sourceName TEXT, sourceInfo TEXT, midiProgramNo INTEGER, categories TEXT)");
				db_.exec("CREATE TABLE IF NOT EXISTS imports (synth TEXT, name TEXT, id TEXT, date TEXT)");

				/*int nb = db_.exec("INSERT INTO test VALUES (NULL, \"test\")");
				std::cout << "INSERT INTO test VALUES (NULL, \"test\")\", returned " << nb << std::endl;*/

				// Commit transaction
				transaction.commit();
			}
		}

		bool putPatch(Synth *activeSynth, PatchHolder const &patch, std::string const &sourceID) {
			try {
				std::string md5 = JsonSerialization::patchMd5(activeSynth, *patch.patch());

				SQLite::Statement sql(db_, "INSERT INTO patches (synth, md5, name, data, favorite, sourceID, sourceName, sourceInfo, midiProgramNo, categories) VALUES (:SYN, :MD5, :NAM, :DAT, :FAV, :SID, :SNM, :SRC, :PRG, :CAT)");

				// Insert values into prepared statement
				sql.bind(":SYN", activeSynth->getName().c_str());
				sql.bind(":MD5", md5);
				sql.bind(":NAM", patch.patch()->patchName());
				sql.bind(":DAT", patch.patch()->data().data(), patch.patch()->data().size());
				sql.bind(":FAV", (int)patch.howFavorite().is());
				sql.bind(":SID", sourceID);
				sql.bind(":SNM", patch.sourceInfo()->toDisplayString(activeSynth));
				sql.bind(":SRC", patch.sourceInfo()->toString());
				sql.bind(":CAT", "");

				auto fileSource = std::dynamic_pointer_cast<FromFileSource>(patch.sourceInfo());
				if (fileSource) {
					sql.bind(":PRG", fileSource->getProgramNumber().toZeroBased());
				}
				else {
					sql.bind(":PRG", patch.patch()->patchNumber()->midiProgramNumber().toZeroBased());
				}

				sql.exec();
			}
			catch (SQLite::Exception &ex) {
				jassert(false);
			}
			return true;
		}

		std::vector<std::pair<std::string, std::string>> getImportsList(Synth *activeSynth) {
			SQLite::Statement query(db_, "SELECT name, id FROM imports WHERE synth = :SYN ORDER BY date");
			query.bind(":SYN", activeSynth->getName());
			std::vector<std::pair<std::string, std::string>> result;
			while (query.executeStep()) {
				result.emplace_back(query.getColumn("name").getText(), query.getColumn("id").getText());
			}
			return result;
		}

		int getPatchesCount(Synth *activeSynth) {
			SQLite::Statement query(db_, "SELECT count(*) FROM patches WHERE synth = :SYN");
			query.bind(":SYN", activeSynth->getName());
			if (query.executeStep()) {
				return query.getColumn(0).getInt();
			}
			return 0;
		}

		bool getPatches(Synth *activeSynth, std::vector<PatchHolder> &result, int skip, int limit) {
			SQLite::Statement query(db_, "SELECT * FROM patches WHERE synth = :SYN ORDER BY sourceID, midiProgramNo LIMIT :LIM OFFSET :OFS");
			query.bind(":SYN", activeSynth->getName());
			query.bind(":LIM", limit);
			query.bind(":OFS", skip);
			while (query.executeStep()) {
				std::shared_ptr<Patch> newPatch;

				// Create the patch itself, from the BLOB stored
				auto dataColumn = query.getColumn("data");
				if (dataColumn.isBlob()) {
					std::vector<uint8> patchData((uint8 *)dataColumn.getBlob(), ((uint8 *)dataColumn.getBlob()) + dataColumn.getBytes());
					newPatch = activeSynth->patchFromPatchData(patchData, "unknown", MidiProgramNumber::fromZeroBase(0));
				}

				if (newPatch) {
					auto sourceColumn = query.getColumn("sourceInfo");
					if (sourceColumn.isText()) {
						PatchHolder holder(SourceInfo::fromString(sourceColumn.getString()), newPatch, false);
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

		std::map<std::string, PatchHolder> bulkGetPatches(Synth *activeSynth, std::vector<PatchHolder> & patches) {
			// Query the database for exactly those patches, we want to know which ones are already there!
			std::map<std::string, PatchHolder> result;

			for (auto ph : patches) {
				// First, calculate list of "IDs"
				std::string md5 = JsonSerialization::patchMd5(activeSynth, *ph.patch());

				// Query the database if this exists. Normally, I would bulk, but as the database is local for now I think we're going to be fine
				try {
					SQLite::Statement query(db_, "SELECT md5 FROM patches WHERE md5 = :MD5");
					query.bind(":MD5", md5);
					if (query.executeStep()) {
						result.emplace(md5, ph);
					}
				} 
				catch (SQLite::Exception &ex) {
					jassert(false);
				}
			}
			return result;
		}

		size_t mergePatchesIntoDatabase(Synth *activeSynth, std::vector<PatchHolder> &patches, std::vector<PatchHolder> &outNewPatches, ProgressHandler *progress) {
			// Generate a UUID that will be used to bind all patches during this import together
			Uuid source_uuid;

			// This works by doing a bulk get operation for the patches from the database...
			auto knownPatches = bulkGetPatches(activeSynth, patches);

			for (auto &patch : patches) {
				std::string md5 = JsonSerialization::patchMd5(activeSynth, *patch.patch());
				if (knownPatches.find(md5) != knownPatches.end()) {
					// Update the loaded info with the database info
					//patch = knownPatches[md5];
				}
				else {
					// This is a new patch - it needs to be uploaded into the database!
					outNewPatches.push_back(patch);
				}
			}

			//TODO can be replaced by repaired bulkPut
			SQLite::Transaction transaction(db_);

			int uploaded = 0;
			for (auto newPatch : outNewPatches) {
				if (progress->shouldAbort()) return uploaded;
				putPatch(activeSynth, newPatch, source_uuid.toString().toStdString());
				uploaded++;
				progress->setProgressPercentage(uploaded / (double)outNewPatches.size());
			}

			if (uploaded > 1) {
				// Record this import in the import table for later filtering! The name of the import might differ for different patches (bulk import), use the first patch to calculate it
				SQLite::Statement sql(db_, "INSERT INTO imports (synth, name, id, date) VALUES (:SYN, :NAM, :SID, datetime('now'))");
				std::string importName = outNewPatches[0].sourceInfo()->toDisplayString(activeSynth);
				sql.bind(":SYN", activeSynth->getName());
				sql.bind(":NAM", importName);
				sql.bind(":SID", source_uuid.toString().toStdString());
				sql.exec();
			}

			transaction.commit();

			return outNewPatches.size();
		}

	private:
		SQLite::Database db_;
	};

	PatchDatabase::PatchDatabase() {
		impl.reset(new PatchDataBaseImpl());
	}

	PatchDatabase::~PatchDatabase() {
	}

	int PatchDatabase::getPatchesCount(Synth *activeSynth)
	{
		return impl->getPatchesCount(activeSynth);
	}

	bool PatchDatabase::putPatch(Synth *activeSynth, PatchHolder const &patch) {
		jassert(false);
		return false;
		//return impl->putPatch(activeSynth, patch);
	}

	bool PatchDatabase::putPatches(Synth *activeSynth, std::vector<PatchHolder> const &patches) {
		jassert(false);
		return false;
	}

	/*void PatchDatabase::runMigration(Synth * activeSynth)
	{
		impl->runMigration(activeSynth);
	}*/

	void PatchDatabase::getPatchesAsync(Synth *activeSynth, std::function<void(std::vector<PatchHolder> const &)> finished, int skip, int limit)
	{
		pool_.addJob([this, activeSynth, finished, skip, limit]() {
			std::vector<PatchHolder> result;
			bool success = impl->getPatches(activeSynth, result, skip, limit);
			if (success) {
				MessageManager::callAsync([finished, result]() { finished(result); });
			}
			else {
				SimpleLogger::instance()->postMessage("Error retrieving patches from the Internet");
			}
		});
	}

	size_t PatchDatabase::mergePatchesIntoDatabase(Synth *activeSynth, std::vector<PatchHolder> &patches, std::vector<PatchHolder> &outNewPatches, ProgressHandler *progress)
	{
		return impl->mergePatchesIntoDatabase(activeSynth, patches, outNewPatches, progress);
	}

	std::vector<std::pair<std::string, std::string>> PatchDatabase::getImportsList(Synth *activeSynth) const {
		return impl->getImportsList(activeSynth);
	}

}

