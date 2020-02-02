/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "PatchHolder.h"
#include "Session.h"

#include <rapidjson/document.h>

namespace midikraft {

	class Synth;

	class JsonSerialization {
	public:
		static std::string patchMd5(Synth *synth, Patch const &patch);
		static std::string patchInSessionID(Synth *synth, std::shared_ptr<SessionPatch> patch);
		static std::string dataToString(std::vector<uint8> const &data);
		static std::vector<uint8> stringToData(std::string const string);
		static std::string patchToJson(Synth *synth, PatchHolder *patchholder);
		static bool jsonToPatch(Synth *activeSynth, rapidjson::Value &patch, PatchHolder &outPatchHolder);
	};

}