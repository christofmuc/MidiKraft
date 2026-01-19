/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchListFill.h"

#include <algorithm>
#include <random>

namespace midikraft {

namespace {

bool matchesActivePatch(PatchHolder const& candidate, PatchHolder const* activePatch) {
	if (!activePatch || !activePatch->patch() || !candidate.patch()) {
		return false;
	}
	auto activeSynth = activePatch->synth();
	auto candidateSynth = candidate.synth();
	if (activeSynth && candidateSynth && activeSynth->getName() != candidateSynth->getName()) {
		return false;
	}
	return candidate.md5() == activePatch->md5();
}

std::vector<PatchHolder> getRandomSubset(const std::vector<PatchHolder>& original, std::size_t subsetSize) {
	std::vector<PatchHolder> shuffled = original;
	if (subsetSize > shuffled.size()) {
		subsetSize = shuffled.size();
	}
	std::random_device rd;
	std::default_random_engine rng(rd());
	std::shuffle(shuffled.begin(), shuffled.end(), rng);
	return std::vector<PatchHolder>(shuffled.begin(), shuffled.begin() + subsetSize);
}

void padToMinimum(std::vector<PatchHolder>& patches, std::size_t minimumCount) {
	while (!patches.empty() && patches.size() < minimumCount) {
		patches.push_back(patches.back());
	}
}

} // namespace

PatchListFillResult fillPatchList(std::vector<PatchHolder> patches,
	const PatchHolder* activePatch,
	PatchListFillRequest const& request) {
	PatchListFillResult result;

	switch (request.mode) {
	case PatchListFillMode::None:
		result.patches = std::move(patches);
		break;
	case PatchListFillMode::Top:
		result.patches = std::move(patches);
		if (request.desiredCount > 0 && result.patches.size() > request.desiredCount) {
			result.patches.resize(request.desiredCount);
		}
		break;
	case PatchListFillMode::FromActive: {
		result.patches = std::move(patches);
		auto activeIt = std::find_if(result.patches.begin(), result.patches.end(),
			[activePatch](PatchHolder const& candidate) {
				return matchesActivePatch(candidate, activePatch);
			});
		if (activeIt != result.patches.end()) {
			result.activePatchFound = true;
			result.patches.erase(result.patches.begin(), activeIt);
		}
		if (request.desiredCount > 0 && result.patches.size() > request.desiredCount) {
			result.patches.resize(request.desiredCount);
		}
		break;
	}
	case PatchListFillMode::Random:
		result.patches = getRandomSubset(patches, request.desiredCount);
		break;
	default:
		result.patches = std::move(patches);
		break;
	}

	padToMinimum(result.patches, request.minimumCount);
	return result;
}

} // namespace midikraft
