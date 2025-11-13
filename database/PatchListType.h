/*
   Copyright (c) 2024 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

namespace midikraft {

	// Authoritative list type values for the lists table (see docs/imports_target_schema.md)
	// TODO: Schema reserves 3 for ACTIVE_SYNTH_BANK/4 for imports; align when migration lands.
	enum PatchListType {
		NORMAL_LIST = 0,
		SYNTH_BANK = 1,
		USER_BANK = 2,
		IMPORT_LIST = 3
	};

}
