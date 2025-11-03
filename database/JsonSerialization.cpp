/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JsonSerialization.h"

#include "JsonSchema.h"
#include "Synth.h"

#include <boost/format.hpp>
#include <boost/beast/core/detail/base64.hpp>
#include <nlohmann/json.hpp>

namespace midikraft {
	namespace {
		using nlohmann::json;

		bool getStringIfSet(json const& dbresult, char const* key, std::string& outString)
		{
			auto it = dbresult.find(key);
			if (it != dbresult.end() && it->is_string()) {
				outString = it->get<std::string>();
				return true;
			}
			return false;
		}

		bool getBufferIfSet(json const& dbresult, char const* key, std::vector<uint8>& outBuffer)
		{
			auto it = dbresult.find(key);
			if (it != dbresult.end() && it->is_string()) {
				outBuffer = JsonSerialization::stringToData(it->get<std::string>());
				return true;
			}
			return false;
		}

		bool getNumberIfSet(json const& dbresult, char const* key, int& out)
		{
			auto it = dbresult.find(key);
			if (it == dbresult.end()) {
				return false;
			}
			if (it->is_number_integer()) {
				out = it->get<int>();
				return true;
			}
			if (it->is_string()) {
				try {
					out = std::stoi(it->get<std::string>());
					return true;
				}
				catch (...) {
					return false;
				}
			}
			return false;
		}
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

		nlohmann::json doc = nlohmann::json::object();
		doc[JsonSchema::kSynth] = synth->getName();
		doc[JsonSchema::kName] = patchholder->patch()->patchName();
		doc[JsonSchema::kSysex] = dataToString(patchholder->patch()->data());
		if (auto realPatch = std::dynamic_pointer_cast<Patch>(patchholder->patch())) {
			std::string numberAsString = (boost::format("%d") % realPatch->patchNumber()->midiProgramNumber().toZeroBased()).str();
			doc[JsonSchema::kPlace] = numberAsString;
		}
		doc[JsonSchema::kMD5] = patchholder->md5();
		return doc.dump();
	}

	bool JsonSerialization::jsonToPatch(Synth *activeSynth, nlohmann::json const &patchDoc, PatchHolder &outPatchHolder, std::shared_ptr<AutomaticCategorizer> categorizer) {
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
			PatchHolder simple(activeSynth, std::make_shared<FromFileSource>("", "", MidiProgramNumber::fromZeroBase(programNo)), newPatch);
			if (categorizer) {
				simple.autoCategorizeAgain(*categorizer);
			}
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
