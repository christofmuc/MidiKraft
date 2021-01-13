/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JsonSchema.h"

namespace midikraft {

	// "Schema definition" for the Documents in the DynamoDB. 
	const char
		//*JsonSchema::kTable = "MKS50Test", // Main table
		*JsonSchema::kTable = "Patches1", // Main table
		*JsonSchema::kTableNew = "Patches3", // Target of schema migration
		*JsonSchema::kSynth = "Synth",
		*JsonSchema::kMD5 = "MD5",
		*JsonSchema::kSource = "Source",
		*JsonSchema::kImport = "Import",
		*JsonSchema::kName = "Name",
		*JsonSchema::kSysex = "Sysex",
		*JsonSchema::kBank = "Bank",
		*JsonSchema::kPlace = "Place",
		*JsonSchema::kPatch = "Patch",
		*JsonSchema::kSession = "Session",
		*JsonSchema::kFavorite = "Favorite",
		*JsonSchema::kCategory = "Category",
		*JsonSchema::kSessionPatchID = "SessionPatchID";

}
