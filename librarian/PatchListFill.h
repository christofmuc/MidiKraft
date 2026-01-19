/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "PatchHolder.h"

#include <cstddef>
#include <vector>

namespace midikraft {

	enum class PatchListFillMode {
		None = 0,
		Top = 1,
		FromActive = 2,
		Random = 3
	};

	struct PatchListFillRequest {
		PatchListFillMode mode = PatchListFillMode::None;
		size_t desiredCount = 0;
		size_t minimumCount = 0;
	};

	struct PatchListFillResult {
		std::vector<PatchHolder> patches;
		bool activePatchFound = false;
	};

	PatchListFillResult fillPatchList(std::vector<PatchHolder> patches,
		const PatchHolder* activePatch,
		PatchListFillRequest const& request);

} // namespace midikraft
