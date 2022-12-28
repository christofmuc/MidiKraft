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

#include "HasBanksCapability.h"
#include "EditBufferCapability.h"
#include "ProgramDumpCapability.h"
#include "BankDumpCapability.h"
#include "DataFileLoadCapability.h"
#include "DataFileSendCapability.h"
#include "StreamLoadCapability.h"
#include "StoredPatchNameCapability.h"
#include "StoredPatchNumberCapability.h"

#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include "SpdLogJuce.h"

namespace midikraft {

	std::string Synth::friendlyProgramName(MidiProgramNumber programNo) const
	{
		// The default implementation is just that you see something
		if (programNo.isBankKnown()) {
			return fmt::format("{:02d}-{:02d}", programNo.bank().toZeroBased(),  programNo.toZeroBased());
		}
		else {
			return fmt::format("{:02d}", programNo.toZeroBased());
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
			// This could be inconsistent - obviously the programNo contains the bank, but you supplied a bank as well!?
			if (bankNo != programNo.bank())
			{
				SimpleLogger::instance()->postMessageOncePerRun("Implementation error - called friendlyProgramAndBankName with inconsistent bank info!");
			}
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
				if (editBufferSynth && editBufferSynth->isMessagePartOfEditBuffer(message).isPartOfEditBufferDump) {
					currentStreak.push_back(message);
					if (editBufferSynth->isEditBufferDump(currentStreak)) {
						auto patch = editBufferSynth->patchFromSysex(currentStreak);
						currentStreak.clear();
						if (patch) {
							result.push_back(patch);
						}
						else {
							spdlog::warn("Error decoding edit buffer dump for patch {}, skipping it", patchNo);
						}
						patchNo++;
					}
				}
				else if (programDumpSynth && programDumpSynth->isMessagePartOfProgramDump(message).isPartOfProgramDump) {
					currentStreak.push_back(message);
					if (programDumpSynth->isSingleProgramDump(currentStreak)) {
						auto patch = programDumpSynth->patchFromProgramDumpSysex(currentStreak);
						currentStreak.clear();
						if (patch) {
							result.push_back(patch);
						}
						else {
							spdlog::warn("Error decoding program dump for patch {}, skipping it",  patchNo);
						}
						patchNo++;
					}
				}
				else if (bankDumpSynth && bankDumpSynth->isBankDump(message)) {
					auto morePatches = bankDumpSynth->patchesFromSysexBank(message);
					spdlog::info("Loaded bank dump with {} patches", morePatches.size());
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
					spdlog::warn("Ignoring sysex message found, not implemented: {}", message.getDescription());
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
				MidiProgramNumber place = MidiProgramNumber::invalidProgram();
				auto defaultPlace = midikraft::Capability::hasCapability<DefaultProgramPlaceInsteadOfEditBufferCapability>(this);
				if (defaultPlace) {
					place = defaultPlace->getDefaultProgramPlace();
				}
				else {
					// Well, where should it go? I'd say last patch of first bank is a good compromise
					auto descriptors = Capability::hasCapability<HasBankDescriptorsCapability>(this);
					if (descriptors) {
						place = MidiProgramNumber::fromZeroBase(descriptors->bankDescriptors()[0].size - 1);
					}
					else {
						auto banks = Capability::hasCapability<HasBanksCapability>(this);
						if (banks) {
							place = MidiProgramNumber::fromZeroBase(banks->numberOfPatches() - 1);
						}
					}
					if (place.isValid()) {
						spdlog::warn("{} has no edit buffer, using program {} instead", getName(), friendlyProgramName(place));
					}
					else {
						spdlog::error("{} has no edit buffer and not way to determine a standard program place, can't send program change", getName());
					}
				}
				messages = programDumpCapability->patchToProgramDumpSysex(dataFile, place);
				auto location = Capability::hasCapability<MidiLocationCapability>(this);
				if (location && location->channel().isValid() && place.isValid()) {
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
			spdlog::error("Program error - unknown strategy to send patch out to synth");
		}
		return messages;
	}

	std::string Synth::nameForPatch(std::shared_ptr<DataFile> dataFile) const {
		// Check if it has a stored name
		auto storedPatchName = Capability::hasCapability<StoredPatchNameCapability>(dataFile);
		if (storedPatchName) {
			return storedPatchName->name();
		}
		// No stored patch name, but we might have a stored number
		auto storedPatchNumber = Capability::hasCapability<StoredPatchNumberCapability>(dataFile);
		if (storedPatchNumber && storedPatchNumber->hasStoredPatchNumber()) {
			return friendlyProgramName(storedPatchNumber->getStoredPatchNumber());
		}
		return "";
	}

	void Synth::sendDataFileToSynth(std::shared_ptr<DataFile> dataFile, std::shared_ptr<SendTarget> target)
	{
		auto messages = dataFileToSysex(dataFile, target);
		if (!messages.empty()) {
			auto midiLocation = midikraft::Capability::hasCapability<MidiLocationCapability>(this);
			if (midiLocation && !messages.empty()) {
				if (midiLocation->channel().isValid()) {
					spdlog::info("Sending patch {} to {}", nameForPatch(dataFile), getName());
					MidiController::instance()->enableMidiOutput(midiLocation->midiOutput());
					sendBlockOfMessagesToSynth(midiLocation->midiOutput(), messages);
				}
				else {
					spdlog::error("Synth {} has no valid channel and output defined, don't know where to send!", getName());
				}
			}
		}
	}

	void Synth::sendBlockOfMessagesToSynth(juce::MidiDeviceInfo const& midiOutput, std::vector<MidiMessage> const& buffer)
	{
		MidiController::instance()->getMidiOutput(midiOutput)->sendBlockOfMessagesFullSpeed(buffer);
	}

}
