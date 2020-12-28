/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

namespace midikraft {

	class JsonSchema {
	public:
		// "Schema definition" for the Documents in the DynamoDB. 
		static const char
			*kTable,
			*kTableNew,
			*kSynth,
			*kMD5,
			*kSource,
			*kImport,
			*kName,
			*kSession,
			*kPatch,
			*kSysex,
			*kBank,
			*kPlace,
			*kFavorite,
			*kCategory,
			*kSessionPatchID
			;
	};

}
