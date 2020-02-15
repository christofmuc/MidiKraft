/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JsonSerialization.h"

#include "JsonSchema.h"
#include "RapidjsonHelper.h"
#include "Synth.h"

#include <boost/format.hpp>
#include <boost/beast/core/detail/base64.hpp>

namespace midikraft {

	bool getStringIfSet(rapidjson::Value &dbresult, const char *key, std::string &outString) {
		if (dbresult.HasMember(key) && dbresult[key].IsString()) {
			outString = dbresult[key].GetString();
			return true;
		}
		return false;
	}

	bool getBufferIfSet(rapidjson::Value &dbresult, const char *key, std::vector<uint8> &outBuffer) {
		if (dbresult.HasMember(key)) {
			outBuffer = JsonSerialization::stringToData(dbresult[key].GetString());
			return true;
		}
		return false;
	}

	bool getNumberIfSet(rapidjson::Value &dbresult, const char *key, int &out) {
		if (dbresult.HasMember(key) && dbresult.IsInt()) {
			out = dbresult[key].GetInt();
			return true;
		}
		return false;
	}

	std::string JsonSerialization::dataToString(std::vector<uint8> const &data) {
		std::string binaryString;
		for (auto byte : data) {
			binaryString.push_back(byte);
		}
		// See https://stackoverflow.com/questions/7053538/how-do-i-encode-a-string-to-base64-using-only-boost
		std::vector<char> buffer(2048);
		size_t lengthWritten = boost::beast::detail::base64::encode(buffer.data(), data.data(), buffer.size());
		jassert(lengthWritten < 2048);
		std::string result(buffer.data(), lengthWritten);
		return result;
	}

	std::vector<uint8> JsonSerialization::stringToData(std::string const string)
	{
		std::vector<uint8> outBuffer(2048, 0);
		auto decoded_bytes = boost::beast::detail::base64::decode(outBuffer.data(), string.c_str(), 2048);
		outBuffer.resize(decoded_bytes.first); // Trim output so it contains only the written part
		return outBuffer;
	}

	std::string JsonSerialization::patchToJson(Synth *synth, PatchHolder *patchholder)
	{
		if (!patchholder || !patchholder->patch() || !synth) {
			jassert(false);
			return "";
		}

		rapidjson::Document doc;
		doc.SetObject();
		addToJson(JsonSchema::kSynth, synth->getName(), doc, doc);
		addToJson(JsonSchema::kName, patchholder->patch()->patchName(), doc, doc);
		addToJson(JsonSchema::kSysex, dataToString(patchholder->patch()->data()), doc, doc);
		std::string numberAsString = (boost::format("%d") % patchholder->patch()->patchNumber()->midiProgramNumber().toZeroBased()).str();
		addToJson(JsonSchema::kPlace, numberAsString, doc, doc);
		addToJson(JsonSchema::kMD5, patchholder->md5(), doc, doc);
		return renderToJson(doc);
	}

	bool JsonSerialization::jsonToPatch(Synth *activeSynth, rapidjson::Value &patchDoc, PatchHolder &outPatchHolder) {
		// Build the patch via the synth from the sysex data...
		std::string name;
		Synth::PatchData data;
		int programNo = 0;
		getStringIfSet(patchDoc, JsonSchema::kName, name);
		getBufferIfSet(patchDoc, JsonSchema::kSysex, data);
		getNumberIfSet(patchDoc, JsonSchema::kPlace, programNo);
		auto newPatch = activeSynth->patchFromPatchData(data, name, MidiProgramNumber::fromZeroBase(programNo));
		if (newPatch != nullptr) {
			/*std::string importInfoJson;
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
			}*/
			PatchHolder simple(activeSynth, std::make_shared<FromFileSource>("", "", MidiProgramNumber::fromZeroBase(programNo)), newPatch, true);
			outPatchHolder = simple;
			return true;
		}
		else {
			return false;
		}
	}

	std::string JsonSerialization::patchInSessionID(Synth *synth, std::shared_ptr<SessionPatch> patch) {
		// Every possible patch can be stored in the database once per synth and session.
		// build a hash to represent this.
		jassert(synth->getName() == patch->synthName_);
		std::string patchHash = patch->patchHolder_.md5();
		std::string toBeHashed = (boost::format("%s-%s-%s") % patch->session_.name_ % patch->synthName_ % patchHash).str();
		MD5 hash(toBeHashed.data(), toBeHashed.size());
		return hash.toHexString().toStdString();
	}

}
