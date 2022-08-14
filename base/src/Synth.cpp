/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Synth.h"

#include "Capability.h"
#include "Patch.h"
#include "MidiController.h"
#include "MidiHelpers.h"
#include "Logger.h"

#include "EditBufferCapability.h"
#include "ProgramDumpCapability.h"
#include "BankDumpCapability.h"
#include "DataFileLoadCapability.h"
#include "DataFileSendCapability.h"
#include "StreamLoadCapability.h"

#include <boost/format.hpp>

namespace midikraft {

	std::string Synth::friendlyProgramName(MidiProgramNumber programNo) const
	{
		// The default implementation is just that you see something
		if (programNo.isBankKnown()) {
			return (boost::format("%02d-%02d") % programNo.bank().toZeroBased() % programNo.toZeroBasedWithoutBank()).str();
		}
		else {
			return (boost::format("%02d") % programNo.toZeroBasedWithoutBank()).str();
		}
	}

	std::string Synth::friendlyProgramAndBankName(MidiBankNumber bankNo, MidiProgramNumber programNo) const
	{
		if (!programNo.isBankKnown()) {
			// Default implementation is the old logic that the program numbers are just continuous
			// from one bank to the next
			int program = programNo.toZeroBased();
			return friendlyProgramName(MidiProgramNumber::fromZeroBaseWithBank(bankNo, program));
		}
		else {
			// This is inconsistent - obviously the programNo contains the bank, but you supplied a bank as well!?
			SimpleLogger::instance()->postMessageOncePerRun("Implementation error - called friendlyProgramAndBankName with program number too big for the bank");
			return friendlyProgramName(programNo);
		}
	}

	Synth::PatchData Synth::filterVoiceRelevantData(std::shared_ptr<DataFile> unfilteredData) const
	{
		// The default implementation does nothing, i.e. all bytes are relevant for the sound of the patch
		// That would be the case e.g. for the Korg DW8000 or the Kawai K3, which have not even a patch name in the patch data
		return unfilteredData->data();
	}

	std::string Synth::calculateFingerprint(std::shared_ptr<DataFile> patch) const
	{
		auto filteredData = filterVoiceRelevantData(patch);
		juce::MD5 md5(&filteredData[0], filteredData.size());
		return md5.toHexString().toStdString();
	}

	std::string Synth::setupHelpText() const
	{
		// Default is nothing special
		return "No special setup information is provided. I'd say, read the manual!";
	}

	TPatchVector Synth::loadSysex(std::vector<MidiMessage> const &sysexMessages)
	{
		TPatchVector result;
		// Now that we have a list of messages, let's see if there are (hopefully) any patches between them
		int patchNo = 0;
		auto editBufferSynth = midikraft::Capability::hasCapability<EditBufferCapability>(this);
		auto programDumpSynth = midikraft::Capability::hasCapability<ProgramDumpCabability>(this);
		auto bankDumpSynth = midikraft::Capability::hasCapability<BankDumpCapability>(this);
		auto dataFileLoadSynth = midikraft::Capability::hasCapability<DataFileLoadCapability>(this);
		auto streamDumpSynth = midikraft::Capability::hasCapability<StreamLoadCapability>(this);
		if (streamDumpSynth) {
			// The stream dump synth loads all at once
			result = streamDumpSynth->loadPatchesFromStream(sysexMessages);
		}
		else {
			// The other Synth types load message by message
			std::vector<MidiMessage> currentStreak;
			for (auto message : sysexMessages) {
				if (editBufferSynth && editBufferSynth->isMessagePartOfEditBuffer(message)) {
					currentStreak.push_back(message);
					if (editBufferSynth->isEditBufferDump(currentStreak)) {
						auto patch = editBufferSynth->patchFromSysex(currentStreak);
						currentStreak.clear();
						if (patch) {
							result.push_back(patch);
						}
						else {
							SimpleLogger::instance()->postMessage((boost::format("Error decoding edit buffer dump for patch %d, skipping it") % patchNo).str());
						}
						patchNo++;
					}
				}
				else if (programDumpSynth && programDumpSynth->isMessagePartOfProgramDump(message)) {
					currentStreak.push_back(message);
					if (programDumpSynth->isSingleProgramDump(currentStreak)) {
						auto patch = programDumpSynth->patchFromProgramDumpSysex(currentStreak);
						currentStreak.clear();
						if (patch) {
							result.push_back(patch);
						}
						else {
							SimpleLogger::instance()->postMessage((boost::format("Error decoding program dump for patch %d, skipping it") % patchNo).str());
						}
						patchNo++;
					}
				}
				else if (bankDumpSynth && bankDumpSynth->isBankDump(message)) {
					auto morePatches = bankDumpSynth->patchesFromSysexBank(message);
					SimpleLogger::instance()->postMessage((boost::format("Loaded bank dump with %d patches") % morePatches.size()).str());
					std::copy(morePatches.begin(), morePatches.end(), std::back_inserter(result));
				}
				else if (dataFileLoadSynth) {
					// Should test all data file types!
					for (int dataType = 0; dataType < static_cast<int>(dataFileLoadSynth->dataTypeNames().size()); dataType++) {
						if (dataFileLoadSynth->isDataFile(message, dataType)) {
							// Hit, we can load this
							auto items = dataFileLoadSynth->loadData({ message }, dataType);
							std::copy(items.begin(), items.end(), std::back_inserter(result));
						}
					}
				}
				else {
					// The way I ended up here was to load the ZIP of the Pro3 factory programs, and that includes the weird macOS resource fork
					// with a syx extension, wrongly getting interpreted as a real sysex file.
					SimpleLogger::instance()->postMessage("Ignoring sysex message found, not implemented: " + message.getDescription());
				}
			}
		}

		return result;
	}

	void Synth::saveSysex(std::string const &filename, std::vector<juce::MidiMessage> messages)
	{
		File outFile = File::createFileWithoutCheckingPath(filename);
		FileOutputStream outStream(outFile);

		for (auto message : messages) {
			outStream.write(message.getRawData(), static_cast<size_t>(message.getRawDataSize()));
		}
	}

	std::vector<juce::MidiMessage> Synth::dataFileToSysex(std::shared_ptr<DataFile> dataFile, std::shared_ptr<SendTarget> target)
	{
		std::vector<MidiMessage> messages;
		if (!target) {
			// Default implementation is to just shoot it to the Midi output and hope for the best, no handshake is done
			// There must be no target specified, for backwards compatibility the old behavior is implemented here to always target the edit buffer of the device
			auto editBufferCapability = midikraft::Capability::hasCapability<EditBufferCapability>(this);
			auto programDumpCapability = midikraft::Capability::hasCapability<ProgramDumpCabability>(this);
			if (editBufferCapability) {
				messages = editBufferCapability->patchToSysex(dataFile);
			}
			else if (programDumpCapability) {
				// There is no edit buffer, we need to ask the device for the default destroyed program number
				MidiProgramNumber place;
				auto defaultPlace = midikraft::Capability::hasCapability<DefaultProgramPlaceInsteadOfEditBufferCapability>(this);
				if (defaultPlace) {
					place = defaultPlace->getDefaultProgramPlace();
				}
				else {
					// Well, where should it go? I'd say last patch of first bank is a good compromise
					place = MidiProgramNumber::fromZeroBaseWithBank(MidiBankNumber::fromZeroBase(0, numberOfPatches()), numberOfPatches() - 1);
					SimpleLogger::instance()->postMessageOncePerRun((boost::format("%s has no edit buffer, using program %s instead") % getName() % friendlyProgramName(place)).str());
				}
				messages = programDumpCapability->patchToProgramDumpSysex(dataFile, place);
				auto location = Capability::hasCapability<MidiLocationCapability>(this);
				if (location && location->channel().isValid()) {
					messages.push_back(MidiMessage::programChange(location->channel().toOneBasedInt(), place.toZeroBased())); // Some synths might need a bank change as well, e.g. the Matrix 1000. Which luckily has an edit buffer
				}
			}
		}
		if (messages.empty()) {
			auto dfcl = midikraft::Capability::hasCapability<DataFileSendCapability>(this);
			if (dfcl) {
				messages = dfcl->dataFileToMessages(dataFile, target);
			}
		}
		if (messages.empty()) {
			jassertfalse;
			SimpleLogger::instance()->postMessage("Program error - unknown strategy to send patch out to synth");
		}
		return messages;
	}

	void Synth::sendDataFileToSynth(std::shared_ptr<DataFile> dataFile, std::shared_ptr<SendTarget> target)
	{
		auto messages = dataFileToSysex(dataFile, target);
		if (!messages.empty()) {
			auto midiLocation = midikraft::Capability::hasCapability<MidiLocationCapability>(this);
			if (midiLocation && !messages.empty()) {
				if (midiLocation->channel().isValid()) {
					SimpleLogger::instance()->postMessage((boost::format("Sending patch %s to %s") % dataFile->name() % getName()).str());
					MidiController::instance()->enableMidiOutput(midiLocation->midiOutput());
					sendBlockOfMessagesToSynth(midiLocation->midiOutput(), messages);
				}
				else {
					SimpleLogger::instance()->postMessage((boost::format("Synth %s has no valid channel and output defined, don't know where to send!") % getName()).str());
				}
			}
		}
	}

	void Synth::sendBlockOfMessagesToSynth(std::string const& midiOutput, std::vector<MidiMessage> const& buffer)
	{
		MidiController::instance()->getMidiOutput(midiOutput)->sendBlockOfMessagesFullSpeed(buffer);
	}

}
