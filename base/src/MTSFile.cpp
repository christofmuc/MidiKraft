/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MTSFile.h"

#include "MidiHelpers.h"

std::string midikraft::MTSFile::patchName() const
{
	MidiTuning result;
	auto sysexMessage = MidiHelpers::sysexMessage(data());
	if (MidiTuning::fromMidiMessage(sysexMessage, result)) {
		return result.name();
	}
	else {
		return "Parse error in MTS file!";
	}
}

void midikraft::MTSFile::setName(std::string const &name)
{
	ignoreUnused(name);
	throw new std::runtime_error("not implemented yet");
}

std::vector<juce::MidiMessage> midikraft::MTSFile::createMidiMessagesFromDataFile(MidiProgramNumber placeToStore)
{
	auto copyOfData = data();
	copyOfData[4] = (uint8) placeToStore.toZeroBased();
	return { MidiHelpers::sysexMessage(copyOfData) };
}
