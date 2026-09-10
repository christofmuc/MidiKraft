/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"
#include "MidiChannel.h"

namespace midikraft {

	// Notifications only: the host sends the returned MIDI to its secondary output.
	// Patch bytes are a snapshot of the library patch, not a request to change it.
	class PatchEventCapability {
	public:
		virtual ~PatchEventCapability() = default;
		virtual std::vector<MidiMessage> onPatchSelected(MidiChannel channel, std::vector<uint8> const& patchData) const = 0;
		virtual std::vector<MidiMessage> onPatchSent(MidiChannel channel, std::vector<uint8> const& patchData) const = 0;
	};

}
