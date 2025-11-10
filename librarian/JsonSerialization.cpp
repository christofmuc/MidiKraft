/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JsonSerialization.h"

#include "JsonSchema.h"
#include "Synth.h"

#include "fmt/format.h"

#include <nlohmann/json.hpp>

namespace midikraft {

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
	
}
