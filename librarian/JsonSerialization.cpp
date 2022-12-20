/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JsonSerialization.h"

#include "JsonSchema.h"
#include "RapidjsonHelper.h"
#include "Synth.h"

#include "fmt/format.h"

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
		return Base64::toBase64(data.data(), data.size()).toStdString();
	}

	std::vector<uint8> JsonSerialization::stringToData(std::string const string)
	{
		std::vector<uint8> outBuffer(2048, 0);
		MemoryOutputStream output(outBuffer.data(), outBuffer.size());
		if (Base64::convertFromBase64(output, string)) {
			return outBuffer;
		}
		else {
			jassertfalse;
			return {};
		}
	}
	
	std::string JsonSerialization::patchInSessionID(Synth *synth, std::shared_ptr<SessionPatch> patch) {
		// Every possible patch can be stored in the database once per synth and session.
		// build a hash to represent this.
		ignoreUnused(synth);
		jassert(synth->getName() == patch->synthName_);
		std::string patchHash = patch->patchHolder_.md5();
		std::string toBeHashed = fmt::format("{}-{}-{}", patch->session_.name_, patch->synthName_, patchHash);
		MD5 hash(toBeHashed.data(), toBeHashed.size());
		return hash.toHexString().toStdString();
	}

}
