/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MTSFile.h"

#include "MidiHelpers.h"

#include <spdlog/spdlog.h>

std::string midikraft::MTSFile::name() const
{
	MidiTuning result;
	auto sysexMessage = MidiHelpers::sysexMessage(data());
	if (MidiTuning::fromMidiMessage(sysexMessage, result)) {
		return result.name();
	}
	else {
		spdlog::error("Parse error in MTS message!");
		return "invalid MTS";
	}
}

bool midikraft::MTSFile::changeNameStoredInPatch(std::string const& name)
{
	ignoreUnused(name);
	spdlog::error("Error - renaming of Midi Tuning Files not implemented yet!");
	return false;
}

std::vector<juce::MidiMessage> midikraft::MTSFile::createMidiMessagesFromDataFile(MidiProgramNumber placeToStore)
{
	auto copyOfData = data();
	copyOfData[4] = (uint8) placeToStore.toZeroBasedDiscardingBank();
	return { MidiHelpers::sysexMessage(copyOfData) };
}
