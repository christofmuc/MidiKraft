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
#include "Sysex.h"

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

#include "Logger.h"

#include <optional>


namespace midikraft {

	std::optional<int> getEnvIfSet(std::string const& env_name) {
		auto userValue = juce::SystemStats::getEnvironmentVariable(env_name, "NOTSET");
		if (userValue != "NOTSET") {
			int numMessages = userValue.getIntValue();
			if (numMessages > 0) {
				SimpleLogger::instance()->postMessageOncePerRun(fmt::format("Overriding maximum number of messages via environment variable {}, value is now {}", env_name, numMessages));
				return numMessages;
			}
			else {
				SimpleLogger::instance()->postMessageOncePerRun(fmt::format("{} environment variable is set, but cannot extract integer from value '{}', ignoring it!", env_name, userValue));
			}
		}
		return {};
	}

	int getEnvWithDefault(std::string const& envName, int defaultValue) {
		auto result = getEnvIfSet(envName);
		if (result.has_value()) {
			return *result;
		}
		else {
			return defaultValue;
		}
	}

	Synth::Synth() {
		maxNumberMessagesPerPatch_ = getEnvWithDefault("ORM_MAX_MSG_PER_PATCH", 14);
		maxNumberMessagesPerBank_ = getEnvWithDefault("ORM_MAX_MSG_PER_BANK", 256);
	}

	std::string Synth::friendlyProgramName(MidiProgramNumber programNo) const
	{
		// The default implementation is just that you see something
		if (programNo.isBankKnown()) {
			return fmt::format("{:02d}-{:02d}", programNo.bank().toZeroBased(),  programNo.toZeroBasedDiscardingBank());
		}
		else {
			return fmt::format("{:02d}", programNo.toZeroBasedWithBank());
		}
	}

	std::string Synth::friendlyProgramAndBankName(MidiBankNumber bankNo, MidiProgramNumber programNo) const
	{
		if (!programNo.isBankKnown()) {
			// Default implementation is the old logic that the program numbers are just continuous
			// from one bank to the next
			int program = programNo.toZeroBasedWithBank();
			return friendlyProgramName(MidiProgramNumber::fromZeroBaseWithBank(bankNo, program));
		}
		else {
			// This could be inconsistent - obviously the programNo contains the bank, but you supplied a bank as well!?
			if (bankNo.toZeroBased() != programNo.bank().toZeroBased())
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
		// Now that we have a list of messages, let's see if there are (hopefully) any patches between them
		auto editBufferSynth = getCapability<EditBufferCapability>();
		auto programDumpSynth = getCapability<ProgramDumpCabability>();
		auto bankDumpSynth = getCapability<BankDumpCapability>();
		auto dataFileLoadSynth = getCapability<DataFileLoadCapability>();
		auto streamDumpSynth = getCapability<StreamLoadCapability>();
		if (streamDumpSynth) {
			// The stream dump synth loads all at once
			return streamDumpSynth->loadPatchesFromStream(sysexMessages);
		}
		else {
			// The other Synth types load message by message
			TPatchVector results;
			std::map<std::string, std::shared_ptr<midikraft::DataFile>> programDumpsById;
			if (programDumpSynth) {
				std::deque<MidiMessage> currentProgramDumps;
				int patchNo = 0;
				for (auto message : sysexMessages) {
					// Try to parse and load these messages as program dumps
					if (programDumpSynth->isMessagePartOfProgramDump(message).isPartOfProgramDump) {
						currentProgramDumps.push_back(message);
						while (currentProgramDumps.size() > maxNumberMessagesPerPatch_) {
							spdlog::debug("Dropping message during parsing as potential number of MIDI messages per patch is larger than {}", maxNumberMessagesPerPatch_);
							currentProgramDumps.pop_front();
						}
						std::vector<MidiMessage> slidingWindow(currentProgramDumps.begin(), currentProgramDumps.end());
						if (programDumpSynth->isSingleProgramDump(slidingWindow)) {
							auto patch = programDumpSynth->patchFromProgramDumpSysex(slidingWindow);
							if (patch) {
								results.push_back(patch);
								programDumpsById[calculateFingerprint(patch)] = patch;
							}
							else {
								spdlog::warn("Error decoding program dump for patch #{}, skipping it. {}", patchNo, Sysex::dumpSysexToString(slidingWindow));
							}
							currentProgramDumps.clear();
							patchNo++;
						}
					}
				}
			}

			if (editBufferSynth) {
				std::deque<MidiMessage> currentEditBuffers;
				// Try to parse and load these messages as edit buffers
				int patchNo = 0;
				for (auto message : sysexMessages) {
					if (editBufferSynth->isMessagePartOfEditBuffer(message).isPartOfEditBufferDump) {
						currentEditBuffers.push_back(message);
						if (currentEditBuffers.size() > maxNumberMessagesPerPatch_) {
							spdlog::debug("Dropping message during parsing as potential number of MIDI messages per patch is larger than {}", maxNumberMessagesPerPatch_);
							currentEditBuffers.pop_front();
						}
						std::vector<MidiMessage> slidingWindow(currentEditBuffers.begin(), currentEditBuffers.end());
						if (editBufferSynth->isEditBufferDump(slidingWindow)) {
							auto patch = editBufferSynth->patchFromSysex(slidingWindow);
							if (patch) {
								auto id = calculateFingerprint(patch);
								if (programDumpsById.find(id) == programDumpsById.end()) {
									results.push_back(patch);
								}
								else {
									// Ignore edit buffer, as we already loaded a program dump with the same ID. This happens for 
									// synths where program dumps will make edit buffers to be detected, like the Reface DX adaptation.
								}
							}
							else {
								spdlog::warn("Error decoding edit buffer dump for patch #{}, skipping it. {}", patchNo, Sysex::dumpSysexToString(slidingWindow));
							}
							currentEditBuffers.clear();
							patchNo++;
						}
					}
				}
			}

			if (bankDumpSynth) {
				std::deque<MidiMessage> currentBank;
				// Try to parse and load these messages as a bank dump
				for (auto message : sysexMessages) {
					if (bankDumpSynth->isBankDump(message)) {
						currentBank.push_back(message);
						if (currentBank.size() > maxNumberMessagesPerBank_) {
							spdlog::debug("Dropping message during parsing as potential number of MIDI messages per patch is larger than {}", maxNumberMessagesPerPatch_);
							currentBank.pop_front();
						}
						std::vector<MidiMessage> slidingWindow(currentBank.begin(), currentBank.end());
						if (bankDumpSynth->isBankDumpFinished(slidingWindow)) {
							auto morePatches = bankDumpSynth->patchesFromSysexBank(slidingWindow);
							spdlog::info("Loaded bank dump with {} patches", morePatches.size());
							std::copy(morePatches.begin(), morePatches.end(), std::back_inserter(results));
							currentBank.clear();
						}
					}
				}
			}

			// Ty to parse and load the message as a data file
			if (dataFileLoadSynth) {
				// Should test all data file types!
				for (auto message : sysexMessages) {
					for (int dataType = 0; dataType < static_cast<int>(dataFileLoadSynth->dataTypeNames().size()); dataType++) {
						if (dataFileLoadSynth->isDataFile(message, dataType)) {
							// Hit, we can load this
							auto items = dataFileLoadSynth->loadData({ message }, dataType);
							std::copy(items.begin(), items.end(), std::back_inserter(results));
						}
					}
				}
			}

			return results;
		}
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
			auto editBufferCapability = getCapability<EditBufferCapability>();
			auto programDumpCapability = getCapability<ProgramDumpCabability>();
			if (editBufferCapability) {
				messages = editBufferCapability->patchToSysex(dataFile);
			}
			else if (programDumpCapability) {
				// There is no edit buffer, we need to ask the device for the default destroyed program number
				MidiProgramNumber place = MidiProgramNumber::invalidProgram();
				auto defaultPlace = getCapability<DefaultProgramPlaceInsteadOfEditBufferCapability>();
				if (defaultPlace) {
					place = defaultPlace->getDefaultProgramPlace();
				}
				else {
					// Well, where should it go? I'd say last patch of first bank is a good compromise
					auto descriptors = getCapability<HasBankDescriptorsCapability>();
					if (descriptors) {
						place = MidiProgramNumber::fromZeroBase(descriptors->bankDescriptors()[0].size - 1);
					}
					else {
						auto banks = getCapability<HasBanksCapability>();
						if (banks) {
							place = MidiProgramNumber::fromZeroBase(banks->numberOfPatches() - 1);
						}
					}
					if (place.isValid()) {
						SimpleLogger::instance()->postMessageOncePerRun(fmt::format("{} has no edit buffer, using program {} instead", getName(), friendlyProgramName(place)));
					}
					else {
						spdlog::error("{} has no edit buffer and not way to determine a standard program place, can't send program change", getName());
					}
				}
				messages = programDumpCapability->patchToProgramDumpSysex(dataFile, place);
				auto location = getCapability<MidiLocationCapability>();
				if (location && location->channel().isValid() && place.isValid()) {
					// Some synths might need a bank change as well, e.g. the Matrix 1000. Which luckily has an edit buffer
					messages.push_back(MidiMessage::programChange(location->channel().toOneBasedInt(), place.toZeroBasedDiscardingBank()));
				}
			}
		}
		if (messages.empty()) {
			auto dfcl = getCapability<DataFileSendCapability>();
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
		auto storedPatchName = dataFile->getCapability<StoredPatchNameCapability>();
		if (storedPatchName) {
			return storedPatchName->name();
		}
		// No stored patch name, but we might have a stored number
		auto storedPatchNumber = dataFile->getCapability<StoredPatchNumberCapability>();
		if (storedPatchNumber && storedPatchNumber->hasStoredPatchNumber()) {
			return friendlyProgramName(storedPatchNumber->getStoredPatchNumber());
		}
		return "";
	}

	MidiProgramNumber Synth::numberForPatch(std::shared_ptr<DataFile> dataFile) 
	{
		// Old school real patch?
		auto realPatch = std::dynamic_pointer_cast<Patch>(dataFile);
		if (realPatch) {
            // This should be the code path for the C++ synth implementations only.
			return realPatch->patchNumber();
		}
		else {
			// Let's check if we have program dump capability
			const auto programDumpCapa = getCapability<ProgramDumpCabability>();
			if (programDumpCapa) {
				// We assume we can interpret the data file as a list of MidiMessages!
				return programDumpCapa->getProgramNumber(dataFile->asMidiMessages());
			}
		}
		return MidiProgramNumber::invalidProgram();
	}

	void Synth::sendDataFileToSynth(std::shared_ptr<DataFile> dataFile, std::shared_ptr<SendTarget> target)
	{
		auto messages = dataFileToSysex(dataFile, target);
		if (!messages.empty()) {
			auto midiLocation = getCapability<MidiLocationCapability>();
			if (midiLocation && !messages.empty()) {
				if (midiLocation->channel().isValid()) {
					auto outputName = midiLocation->midiOutput().name.toStdString();
					spdlog::debug("Data file sent is '{}' for synth {} to device {}", nameForPatch(dataFile), getName(), outputName);
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


	int Synth::sizeOfBank(std::shared_ptr<Synth> synth, int zeroBasedBankNumber)
	{
		auto descriptors = synth->getCapability<HasBankDescriptorsCapability>();
		if (descriptors) {
			return descriptors->bankDescriptors()[zeroBasedBankNumber].size;
		}
		else {
			auto banks = synth->getCapability<HasBanksCapability>();
			if (banks) {
				return banks->numberOfPatches();
			}
		}
		return -1;
	}

	MidiBankNumber Synth::bankNumberFromInt(std::shared_ptr<Synth> synth, int zeroBasedBankNumber)
	{
		return MidiBankNumber::fromZeroBase(zeroBasedBankNumber, Synth::sizeOfBank(synth, zeroBasedBankNumber));
	}

	CapabilityRegistry globalCapabilityRegistry;

}
