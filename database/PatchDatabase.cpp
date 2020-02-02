/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchDatabase.h"

#include "Patch.h"
#include "Logger.h"

#include "JsonSchema.h"
#include "JsonSerialization.h"

#include "ProgressHandler.h"

#include <aws/core/Aws.h>
#include <aws/core/utils/Outcome.h> 
#include <aws/dynamodb/DynamoDBClient.h>
#include <aws/dynamodb/model/ListTablesRequest.h>
#include <aws/dynamodb/model/ListTablesResult.h>

#include <aws/dynamodb/model/AttributeDefinition.h>
#include <aws/dynamodb/model/PutItemRequest.h>
#include <aws/dynamodb/model/PutItemResult.h>

#include <aws/dynamodb/model/UpdateItemResult.h>

#include <aws/dynamodb/model/QueryRequest.h>
#include <aws/dynamodb/model/QueryResult.h>

#include <aws/dynamodb/model/BatchGetItemRequest.h>
#include <aws/dynamodb/model/BatchGetItemResult.h>

#include <aws/dynamodb/model/BatchWriteItemRequest.h>

#include <iostream>
#include <boost/format.hpp>

#include "Dynamo.h"

namespace midikraft {

	class PatchDatabase::PatchDataBaseImpl {
	public:
		PatchDataBaseImpl(Aws::Client::ClientConfiguration config) : clientConfig(config), dynamoClient(clientConfig) {
		}

		class MigratePatches : public ThreadWithProgressWindow {
		public:
			MigratePatches(Synth *activeSynth, PatchDataBaseImpl *db) :
				ThreadWithProgressWindow("Migrating patches to new table", true, true),
				activeSynth_(activeSynth), db_(db) {}

			// Implementation of task
			virtual void run() override {
				// Load all patches for this synth
				std::vector<PatchHolder> patches;
				db_->getPatches(activeSynth_, patches);

				// There will be duplicates in here, but we just store them all to a new table
				int count = 0;
				for (auto patch : patches) {
					if (threadShouldExit()) {
						break;
					}
					db_->putPatch(activeSynth_, patch, JsonSchema::kTableNew);
					count++;
					setProgress(count / (double)patches.size());
				}
			}

		private:
			Synth *activeSynth_;
			PatchDataBaseImpl *db_;
		};

		void runMigration(Synth * activeSynth)
		{
			MigratePatches migrationThread(activeSynth, this);
			if (migrationThread.runThread()) {
				SimpleLogger::instance()->postMessage("Migration completed, now switch the table to be used in the source code");
			}
			else {
				SimpleLogger::instance()->postMessage("Migration canceled");
			}
		}


		bool getPatches(Synth *activeSynth, std::vector<PatchHolder> &outPatches) {
			DynamoQuery query(JsonSchema::kTable, JsonSchema::kSynth, activeSynth->getName());

			// Retrieve all Patches for this synth
			return query.fetchResults(dynamoClient, [this, activeSynth, &outPatches](TDynamoMap &patch) {
				PatchHolder metaData;
				if (itemToPatch(activeSynth, patch, metaData)) {
					outPatches.push_back(metaData);
				}
				else {
					SimpleLogger::instance()->postMessage("Error parsing patch data");
				}
			});
		}

		std::map<std::string, PatchHolder> bulkGetPatches(Synth *activeSynth, std::vector<PatchHolder> & patches, ProgressHandler *progress) const
		{
			std::map<std::string, PatchHolder> alreadyKnownPatches;

			// Max 100 per roundtrip...
			int base = 0;
			while (base < (int) patches.size()) {
				Aws::DynamoDB::Model::BatchGetItemRequest request;
				Aws::DynamoDB::Model::KeysAndAttributes items;

				// DynamoDB throws an error when you hand in a key more than once, so we must make sure this doesn't happen
				std::set<std::string> alreadyListed;
				for (size_t i = 0; i < std::min((size_t)100, patches.size() - base); i++) {
					Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> itemKeys;
					itemKeys.emplace(JsonSchema::kSynth, Aws::DynamoDB::Model::AttributeValue().SetS(activeSynth->getName().c_str()));
					std::string md5 = JsonSerialization::patchMd5(activeSynth, *patches[i + base].patch());
					itemKeys.emplace(JsonSchema::kMD5, Aws::DynamoDB::Model::AttributeValue().SetS(md5.c_str()));

					std::string uniqueString = activeSynth->getName() + md5;
					if (alreadyListed.find(uniqueString) == alreadyListed.end()) {
						alreadyListed.insert(uniqueString);
						items.AddKeys(itemKeys);
					}
				}
				request.AddRequestItems(JsonSchema::kTable, items);

				auto result = dynamoClient.BatchGetItem(request);
				progress->setProgressPercentage(base + alreadyListed.size() / (double)patches.size());
				if (result.IsSuccess()) {
					for (auto found : result.GetResult().GetResponses()) {
						if (found.first == JsonSchema::kTable) {
							for (auto item : found.second) {
								PatchHolder metaData;
								if (itemToPatch(activeSynth, item, metaData)) {
									alreadyKnownPatches[JsonSerialization::patchMd5(activeSynth, *metaData.patch())] = metaData;
								}
								else {
									SimpleLogger::instance()->postMessage("Error parsing patch from db");
								}
							}
						}
						else {
							SimpleLogger::instance()->postMessage("BatchGetItem returned data from wrong table");
						}
					}
				}
				else {
					SimpleLogger::instance()->postMessage("Error: " + toString(result.GetError().GetMessage()));
				}

				base += 100;
			}
			return alreadyKnownPatches;
		}


		bool putPatch(Synth *activeSynth, PatchHolder const &patch, const char *tableName = JsonSchema::kTable) {
			// Update all values
			auto item = patchToItem(activeSynth, *patch.patch());
			DynamoUpdateItem pir(tableName, keys);
			for (auto key : keys) pir.AddKey(key, item[key]);

			// Store the favorite only when we have a user decision - else don't overwrite the wisdom of the database
			std::string setFavoriteExpression = "";
			if (patch.howFavorite().is() != Favorite::TFavorite::DONTKNOW) {
				setFavoriteExpression = ", #favorite = :f";
				pir.AddExpressionAttributeNames("#favorite", JsonSchema::kFavorite);
				pir.AddExpressionAttributeValues(":f", Aws::DynamoDB::Model::AttributeValue().SetBool(patch.isFavorite()));
			}

			// Store source only when we have that information
			std::string setSourceExpression = "";
			bool setsource = false;
			if (patch.sourceInfo()) {
				setSourceExpression = ", #source = :s";
				setsource = true;
			}

			// Add categories
			pir.AddExpressionAttributeNames("#category", JsonSchema::kCategory);
			Aws::DynamoDB::Model::AttributeValue cats;
			for (auto cat : patch.categories()) {
				cats.AddSItem(cat.category.c_str());
			}
			if (cats.GetSS().empty()) {
				// You're not allowed to write an empty set with set
				cats.AddSItem("empty");
			}
			pir.AddExpressionAttributeValues(":c", cats);

			// This is to append to the source set
			pir.SetUpdateExpression((boost::format("SET #name = if_not_exists(#name, :n), #category = :c, #sysex = :x, #place = :p%s %s") % setFavoriteExpression % setSourceExpression).str().c_str());
			pir.AddExpressionAttributeNames("#name", JsonSchema::kName);
			pir.AddExpressionAttributeNames("#sysex", JsonSchema::kSysex);
			pir.AddExpressionAttributeNames("#place", JsonSchema::kPlace);
			if (setsource) pir.AddExpressionAttributeNames("#source", JsonSchema::kImport);
			if (setsource) pir.AddExpressionAttributeValues(":s", Aws::DynamoDB::Model::AttributeValue().SetS(patch.sourceInfo()->toString().c_str()));
			pir.AddExpressionAttributeValues(":n", item[JsonSchema::kName]);
			pir.AddExpressionAttributeValues(":x", item[JsonSchema::kSysex]);
			pir.AddExpressionAttributeValues(":p", item[JsonSchema::kPlace]);

			const Aws::DynamoDB::Model::UpdateItemOutcome result = dynamoClient.UpdateItem(pir);
			if (!result.IsSuccess())
			{
				SimpleLogger::instance()->postMessage(toString(result.GetError().GetMessage()));
				return false;
			}
			return true;
		}

		bool putPatches(Synth *activeSynth, std::vector<PatchHolder> const &patches) {
			Aws::Vector<Aws::DynamoDB::Model::WriteRequest> items;
			int batchSize = 0;
			for (auto patch : patches) {
				if (patch.patch()) {
					auto item = patchToItem(activeSynth, *patch.patch());
					items.push_back(Aws::DynamoDB::Model::WriteRequest().
						WithPutRequest(Aws::DynamoDB::Model::PutRequest().
							WithItem(item)));
					batchSize++;
					if (batchSize == 25) {
						if (!sendBatch(items)) {
							return false;
						}
						batchSize = 0;
						items.clear();
					}
				}
			}
			if (items.size() > 0) {
				if (!sendBatch(items)) {
					return false;
				}
			}
			return true;
		}

		size_t mergePatchesIntoDatabase(Synth *activeSynth, std::vector<PatchHolder> &patches, std::vector<PatchHolder> &outNewPatches, ProgressHandler *progress)
		{
			// This works by doing a bulk get operation for the patches from the database...
			auto knownPatches = bulkGetPatches(activeSynth, patches, progress);

			for (auto &patch : patches) {
				std::string md5 = JsonSerialization::patchMd5(activeSynth, *patch.patch());
				if (knownPatches.find(md5) != knownPatches.end()) {
					// Update the loaded info with the database info
					patch = knownPatches[md5];
				}
				else {
					// This is a new patch - it needs to be uploaded into the database!
					outNewPatches.push_back(patch);
				}
			}

			//TODO can be replaced by repaired bulkPut
			int uploaded = 0;
			for (auto newPatch : outNewPatches) {
				if (progress->shouldAbort()) return uploaded;
				putPatch(activeSynth, newPatch);
				uploaded++;
				progress->setProgressPercentage(uploaded / (double)outNewPatches.size());
			}

			return outNewPatches.size();
		}


	private:
		std::set<const char *> keys = { JsonSchema::kSynth, JsonSchema::kMD5 };

		DynamoDict patchToItem(Synth *activeSynth, Patch const &patch) {
			DynamoDict attrs;

			attrs.addAttribute(JsonSchema::kSynth, activeSynth->getName());
			attrs.addAttribute(JsonSchema::kName, patch.patchName());
			attrs.addAttribute(JsonSchema::kSysex, patch.data());

			// We could have a place defined, a midi program number
			if (patch.patchNumber()) {
				attrs.addAttribute(JsonSchema::kPlace, patch.patchNumber()->midiProgramNumber().toZeroBased());
			}
			attrs.addAttribute(JsonSchema::kMD5, JsonSerialization::patchMd5(activeSynth, patch));
			return attrs;
		}

		bool itemToPatch(Synth *activeSynth, Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> &patch, PatchHolder &outPatchHolder) const {
			// Build the patch via the synth from the sysex data...
			std::string name;
			Synth::PatchData data;
			int programNo = 0;
			getStringIfSet(patch, JsonSchema::kName, name);
			getBufferIfSet(patch, JsonSchema::kSysex, data);
			getNumberIfSet(patch, JsonSchema::kPlace, programNo);
			auto newPatch = activeSynth->patchFromPatchData(data, name, MidiProgramNumber::fromZeroBase(programNo));
			if (newPatch != nullptr) {
				std::string importInfoJson;
				getStringIfSet(patch, JsonSchema::kImport, importInfoJson);
				PatchHolder withMeta(SourceInfo::fromString(importInfoJson), newPatch, patch.find(JsonSchema::kCategory) == patch.end()); // If there is no category field in the database, allow to autodetect
				bool fav = false;
				if (getBoolIfSet(patch, JsonSchema::kFavorite, fav)) {
					withMeta.setFavorite(Favorite(fav));
				}
				std::vector<std::string> categories;
				if (getStringSetIfSet(patch, JsonSchema::kCategory, categories)) {
					for (auto cat : categories) {
						withMeta.setCategory(cat, true);
					}
				}
				outPatchHolder = withMeta;
				return true;
			}
			else {
				return false;
			}
		}

		bool sendBatch(Aws::Vector<Aws::DynamoDB::Model::WriteRequest> const &items) {
			Aws::Map<Aws::String, Aws::Vector<Aws::DynamoDB::Model::WriteRequest>> batch;
			batch.emplace(JsonSchema::kTable, items);
			Aws::DynamoDB::Model::BatchWriteItemRequest bwir;
			auto result = dynamoClient.BatchWriteItem(bwir.WithRequestItems(batch));
			if (!result.IsSuccess())
			{
				SimpleLogger::instance()->postMessage(toString(result.GetError().GetMessage()));
				return false;
			}
			return true;
		}

		Aws::Client::ClientConfiguration clientConfig;
		Aws::DynamoDB::DynamoDBClient dynamoClient;
	};

	PatchDatabase::PatchDatabase() {
		Aws::Client::ClientConfiguration config;
		// no proxy :-)
		config.region = Aws::Region::EU_CENTRAL_1; // Frankfurt
		impl.reset(new PatchDataBaseImpl(config));
	}

	PatchDatabase::~PatchDatabase() {
	}

	bool PatchDatabase::putPatch(Synth *activeSynth, PatchHolder const &patch) {
		return impl->putPatch(activeSynth, patch);
	}

	bool PatchDatabase::putPatches(Synth *activeSynth, std::vector<PatchHolder> const &patches) {
		return impl->putPatches(activeSynth, patches);
	}

	void PatchDatabase::runMigration(Synth * activeSynth)
	{
		impl->runMigration(activeSynth);
	}

	void PatchDatabase::getPatchesAsync(Synth *activeSynth, std::function<void(std::vector<PatchHolder> const &)> finished)
	{
		pool_.addJob([this, activeSynth, finished]() {
			std::vector<PatchHolder> result;
			bool success = impl->getPatches(activeSynth, result);
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

}

