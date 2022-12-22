/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "RapidjsonHelper.h"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

rapidjson::Value value(std::string const &string, rapidjson::Document &document) {
	rapidjson::Value newValue;
	// This has copy semantics, so the string can be destroyed afterwards
	newValue.SetString(string.c_str(), document.GetAllocator());
	return newValue;
}

std::string renderToJson(rapidjson::Document &doc) {
	rapidjson::StringBuffer sb;
	rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
	doc.Accept(writer);
	return sb.GetString();
}

std::string renderToJson(rapidjson::Value const &value) {
	rapidjson::StringBuffer sb;
	rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
	value.Accept(writer);
	return sb.GetString();
}


void addToJson(std::string const &key, std::string const &data, rapidjson::Value &object, rapidjson::Document &doc) {
	object.AddMember(value(key, doc), value(data, doc), doc.GetAllocator());
}
