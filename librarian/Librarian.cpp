/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Librarian.h"

#include "Synth.h"
#include "StepSequencer.h"
#include "Sysex.h"
#include "HasBanksCapability.h"
#include "BankDumpCapability.h"
#include "EditBufferCapability.h"
#include "ProgramDumpCapability.h"
#include "StreamLoadCapability.h"
#include "HandshakeLoadingCapability.h"
#include "LegacyLoaderCapability.h"
#include "SendsProgramChangeCapability.h"
#include "PatchInterchangeFormat.h"

#include "RunWithRetry.h"
#include "MidiHelpers.h"
#include "FileHelpers.h"

#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <set>
#include "Settings.h"
#include "SpdLogJuce.h"

namespace midikraft {

	Librarian::~Librarian() {
		clearHandlers();
	}

	void Librarian::startDownloadingAllPatches(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, std::vector<MidiBankNumber> bankNo,
		ProgressHandler* progressHandler, TFinishedHandler onFinished) {

		downloadBankNumber_ = 0;
		if (!bankNo.empty()) {
			currentDownloadedPatches_.clear();
			nextBankHandler_ = [this, midiOutput, synth, progressHandler, bankNo, onFinished](std::vector<midikraft::PatchHolder> patchesLoaded) {
				std::copy(patchesLoaded.begin(), patchesLoaded.end(), std::back_inserter(currentDownloadedPatches_));
				downloadBankNumber_++;
				if (downloadBankNumber_ == static_cast<int>(bankNo.size())) {
					if (bankNo.size() > 1) {
						tagPatchesWithMultiBulkImport(currentDownloadedPatches_);
					}
					onFinished(currentDownloadedPatches_);
				}
				else {
					if (!progressHandler->shouldAbort()) {
						progressHandler->setMessage(fmt::format("Importing {} from {}...", SynthBank::friendlyBankName(synth, bankNo[(size_t) downloadBankNumber_]), synth->getName()));
						startDownloadingAllPatches(midiOutput, synth, bankNo[(size_t) downloadBankNumber_], progressHandler, nextBankHandler_);
					}
				}
			};
			progressHandler->setMessage(fmt::format("Importing {} from {}...", SynthBank::friendlyBankName(synth, bankNo[0]), synth->getName()));
			startDownloadingAllPatches(midiOutput, synth, bankNo[0], progressHandler, nextBankHandler_);
		}
	}

	BankDownloadMethod Librarian::determineBankDownloadMethod(std::shared_ptr<Synth> synth) {
		if (auto bankMethod = midikraft::Capability::hasCapability<BankDownloadMethodIndicationCapability>(synth)) {
			auto preferredMethod = bankMethod->bankDownloadMethod();
			if (preferredMethod != BankDownloadMethod::UNKNOWN) {
				return preferredMethod;
			}
		}

		// Default behavior - use the "best possible"
		if (midikraft::Capability::hasCapability<StreamLoadCapability>(synth)) {
			return BankDownloadMethod::STREAMING;
		}
		else if (midikraft::Capability::hasCapability<HandshakeLoadingCapability>(synth)) {
			return BankDownloadMethod::HANDSHAKES;
		}
		else if (midikraft::Capability::hasCapability<BankDumpRequestCapability>(synth)) {
			return BankDownloadMethod::BANKS;
		}
		else if (midikraft::Capability::hasCapability<ProgramDumpCabability>(synth)) {
			return BankDownloadMethod::PROGRAM_BUFFERS;
		}
		else if (midikraft::Capability::hasCapability<EditBufferCapability>(synth)) {
			return BankDownloadMethod::EDIT_BUFFERS;
		}
		else {
			return BankDownloadMethod::UNKNOWN;
		}
	}

	void Librarian::startDownloadingAllPatches(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, MidiBankNumber bankNo,
		ProgressHandler* progressHandler, TFinishedHandler onFinished)
	{
		// First things first - this should not be called more than once at a time, and there should be no other Librarian callback handlers be registered!
		jassert(handles_.empty());
		clearHandlers();

		// Ok, for this we need to send a program change message, and then a request edit buffer message from the active synth
		// Once we get that, store the patch and increment number by one
		downloadNumber_ = 0;
		currentDownload_.clear();
		onFinished_ = onFinished;

		// Determine what we will do with the answer...
		auto handle = MidiController::makeOneHandle();

		switch (determineBankDownloadMethod(synth)) {
		case BankDownloadMethod::STREAMING: {
			auto streamLoading = midikraft::Capability::hasCapability<StreamLoadCapability>(synth);
			// Simple enough, we hope
			MidiController::instance()->addMessageHandler(handle, [this, synth, progressHandler, midiOutput](MidiInput* source, const juce::MidiMessage& editBuffer) {
				ignoreUnused(source);
				this->handleNextStreamPart(midiOutput, synth, progressHandler, editBuffer, StreamLoadCapability::StreamType::BANK_DUMP);
				});
			handles_.push(handle);
			currentDownloadBank_ = bankNo;
			expectedDownloadNumber_ = SynthBank::numberOfPatchesInBank(synth, bankNo);
			if (expectedDownloadNumber_ > 0) {
				auto messages = streamLoading->requestStreamElement(bankNo.toZeroBased(), StreamLoadCapability::StreamType::BANK_DUMP);
				synth->sendBlockOfMessagesToSynth(midiOutput->deviceInfo(), messages);
			}
		}
			break;
		case BankDownloadMethod::HANDSHAKES: {
			auto handshakeLoadingRequired = midikraft::Capability::hasCapability<HandshakeLoadingCapability>(synth);
			// These are proper protocols that are implemented - each message we get from the synth has to be answered by an appropriate next message
			std::shared_ptr<HandshakeLoadingCapability::ProtocolState>  state = handshakeLoadingRequired->createStateObject();
			if (state) {
				MidiController::instance()->addMessageHandler(handle, [this, handshakeLoadingRequired, state, progressHandler, midiOutput, synth, bankNo](MidiInput* source, const juce::MidiMessage& protocolMessage) {
					ignoreUnused(source);
					std::vector<MidiMessage> answer;
					if (handshakeLoadingRequired->isNextMessage(protocolMessage, answer, state)) {
						// Store message
						currentDownload_.push_back(protocolMessage);
					}
					// Send an answer if the handshake handler constructed one
					if (!answer.empty()) {
						synth->sendBlockOfMessagesToSynth(midiOutput->deviceInfo(), answer);
					}
					// Update progress handler
					progressHandler->setProgressPercentage(state->progress());

					// Stop handler when finished
					if (state->isFinished() || progressHandler->shouldAbort()) {
						clearHandlers();
						if (state->wasSuccessful()) {
							// Parse patches and send them back 
							auto patches = synth->loadSysex(currentDownload_);
							onFinished_(tagPatchesWithImportFromSynth(synth, patches, bankNo));
							progressHandler->onSuccess();
						}
						else {
							progressHandler->onCancel();
						}
					}
					});
				handles_.push(handle);
				handshakeLoadingRequired->startDownload(midiOutput, state);
			}
			else {
				jassert(false);
			}
			break;
		}
		case BankDownloadMethod::BANKS: {
			auto bankCapableSynth = midikraft::Capability::hasCapability<BankDumpRequestCapability>(synth);
			// This is a mixture - you send one message (bank request), and then you get either one message back (like Kawai K3) or a stream of messages with
			// one message per patch (e.g. Access Virus or Matrix1000)
			auto buffer = bankCapableSynth->requestBankDump(bankNo);
			auto outname = midiOutput->deviceInfo();
			auto timestampOfLastMessage = std::make_shared<juce::Time>(juce::Time::getCurrentTime());
			expectedDownloadNumber_ = SynthBank::numberOfPatchesInBank(synth, bankNo);
			MidiController::instance()->addMessageHandler(handle, [this, synth, progressHandler, midiOutput, bankNo, timestampOfLastMessage](MidiInput* source, const juce::MidiMessage& editBuffer) {
				ignoreUnused(source);
				auto now = juce::Time::getCurrentTime();
				std::swap(*timestampOfLastMessage, now);  // Update last received message time
				this->handleNextBankDump(midiOutput, synth, progressHandler, editBuffer, bankNo);
				});
			auto partialHandle = MidiController::makeOneHandle();
			MidiController::instance()->addPartialMessageHandler(partialHandle, [timestampOfLastMessage](MidiInput* source, const uint8* data, int numBytesSoFar, double timestamp) {
				ignoreUnused(source, data, numBytesSoFar, timestamp);
				auto now = juce::Time::getCurrentTime();
				std::swap(*timestampOfLastMessage, now);
				});
			handles_.push(handle);
			handles_.push(partialHandle);
			currentDownload_.clear();
			synth->sendBlockOfMessagesToSynth(outname, buffer);
			break;
		}
		case BankDownloadMethod::PROGRAM_BUFFERS: {
			// Uh, stone age, need to start a loop
			auto programDumpCapability = midikraft::Capability::hasCapability<ProgramDumpCabability>(synth);
			if (programDumpCapability) {
				MidiController::instance()->addMessageHandler(handle, [this, synth, progressHandler, midiOutput, bankNo](MidiInput* source, const juce::MidiMessage& editBuffer) {
					ignoreUnused(source);
					this->handleNextProgramBuffer(midiOutput, synth, progressHandler, editBuffer, bankNo);
					});
				handles_.push(handle);
				downloadNumber_ = SynthBank::startIndexInBank(synth, bankNo);
				startDownloadNumber_ = downloadNumber_;
				endDownloadNumber_ = downloadNumber_ + SynthBank::numberOfPatchesInBank(synth, bankNo);
				startDownloadNextPatch(midiOutput, synth);
			}
			break;
		}
		case BankDownloadMethod::EDIT_BUFFERS: {
			// Uh, stone age, need to start a loop
			auto editBufferCapability = midikraft::Capability::hasCapability<EditBufferCapability>(synth);
			if (editBufferCapability) {
				MidiController::instance()->addMessageHandler(handle, [this, synth, progressHandler, midiOutput, bankNo](MidiInput* source, const juce::MidiMessage& editBuffer) {
					ignoreUnused(source);
					this->handleNextEditBuffer(midiOutput, synth, progressHandler, editBuffer, bankNo);
					});
				handles_.push(handle);
				downloadNumber_ = SynthBank::startIndexInBank(synth, bankNo);
				startDownloadNumber_ = downloadNumber_;
				endDownloadNumber_ = downloadNumber_ + SynthBank::numberOfPatchesInBank(synth, bankNo);
				startDownloadNextEditBuffer(midiOutput, synth, true);
			}
			break;
		}
        case BankDownloadMethod::UNKNOWN:
		default:
			spdlog::error("Error: This synth has not implemented a single method to retrieve a bank. Please consult the documentation!");
		}
	}

	void Librarian::downloadEditBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler* progressHandler, TFinishedHandler onFinished)
	{
		// First things first - this should not be called more than once at a time, and there should be no other Librarian callback handlers be registered!
		jassert(handles_.empty());
		clearHandlers();

		// Ok, for this we need to send a program change message, and then a request edit buffer message from the active synth
		// Once we get that, store the patch and increment number by one
		downloadNumber_ = 0;
		currentDownload_.clear();
		onFinished_ = onFinished;
		auto editBufferCapability = midikraft::Capability::hasCapability<EditBufferCapability>(synth);
		auto streamLoading = midikraft::Capability::hasCapability<StreamLoadCapability>(synth);
		auto programDumpCapability = midikraft::Capability::hasCapability<ProgramDumpCabability>(synth);
		auto programChangeCapability = midikraft::Capability::hasCapability<SendsProgramChangeCapability>(synth);
		auto handle = MidiController::makeOneHandle();
		if (streamLoading) {
			// Simple enough, we hope
			MidiController::instance()->addMessageHandler(handle, [this, synth, progressHandler, midiOutput](MidiInput* source, const juce::MidiMessage& editBuffer) {
				ignoreUnused(source);
				this->handleNextStreamPart(midiOutput, synth, progressHandler, editBuffer, StreamLoadCapability::StreamType::EDIT_BUFFER_DUMP);
				});
			handles_.push(handle);
			currentDownload_.clear();
			auto messages = streamLoading->requestStreamElement(0, StreamLoadCapability::StreamType::EDIT_BUFFER_DUMP);
			synth->sendBlockOfMessagesToSynth(midiOutput->deviceInfo(), messages);
		}
		else if (editBufferCapability) {
			MidiController::instance()->addMessageHandler(handle, [this, synth, progressHandler, midiOutput](MidiInput* source, const juce::MidiMessage& editBuffer) {
				ignoreUnused(source);
				this->handleNextEditBuffer(midiOutput, synth, progressHandler, editBuffer, MidiBankNumber::fromZeroBase(0, SynthBank::numberOfPatchesInBank(synth, 0)));
				});
			handles_.push(handle);
			// Special case - load only a single patch. In this case we're interested in the edit buffer only!
			startDownloadNumber_ = 0;
			endDownloadNumber_ = 1;
			startDownloadNextEditBuffer(midiOutput, synth, false); // No program change required, we want exactly one edit buffer, the current one
		}
		else if (programDumpCapability && programChangeCapability) {
			auto messages = programDumpCapability->requestPatch(programChangeCapability->lastProgramChange().toZeroBasedWithBank());
			synth->sendBlockOfMessagesToSynth(midiOutput->deviceInfo(), messages);
		}
		else {
			spdlog::error("The {} has no way to request the edit buffer or program place", synth->getName());
		}
	}

	void Librarian::startDownloadingSequencerData(std::shared_ptr<SafeMidiOutput> midiOutput, DataFileLoadCapability* sequencer, int dataFileIdentifier, ProgressHandler* progressHandler, TStepSequencerFinishedHandler onFinished)
	{
		// First things first - this should not be called more than once at a time, and there should be no other Librarian callback handlers be registered!
		jassert(handles_.empty());
		clearHandlers();

		downloadNumber_ = 0;
		currentDownload_.clear();
		onSequencerFinished_ = onFinished;

		auto handle = MidiController::makeOneHandle(); 
		MidiController::instance()->addMessageHandler(handle, [this, sequencer, progressHandler, midiOutput, dataFileIdentifier](MidiInput* source, const MidiMessage& message) {
			ignoreUnused(source);
			if (sequencer->isDataFile(message, dataFileIdentifier)) {
				currentDownload_.push_back(message);
				downloadNumber_++;
				if (downloadNumber_ >= sequencer->numberOfDataItemsPerType(dataFileIdentifier)) {
					auto loadedData = sequencer->loadData(currentDownload_, dataFileIdentifier);
					clearHandlers();
					onSequencerFinished_(loadedData);
					if (progressHandler) progressHandler->onSuccess();
				}
				else if (progressHandler->shouldAbort()) {
					clearHandlers();
					if (progressHandler) progressHandler->onCancel();
				}
				else {
					startDownloadNextDataItem(midiOutput, sequencer, dataFileIdentifier);
					if (progressHandler) progressHandler->setProgressPercentage(downloadNumber_ / (double)sequencer->numberOfDataItemsPerType(dataFileIdentifier));
				}
			}
			});
		handles_.push(handle);
		startDownloadNextDataItem(midiOutput, sequencer, dataFileIdentifier);
	}

	Synth* Librarian::sniffSynth(std::vector<MidiMessage> const& messages) const
	{
		std::set<Synth*> result;
		for (auto message : messages) {
			for (auto synth : synths_) {
				if (synth.synth() && synth.synth()->isOwnSysex(message)) {
					result.insert(synth.synth().get());
				}
			}
		}
		if (result.size() > 1) {
			// oh oh
			jassert(false);
			return *result.begin();
		}
		else if (result.size() == 1) {
			return *result.begin();
		}
		else {
			return nullptr;
		}
	}

	class LoadManyPatchFiles : public ThreadWithProgressWindow {
	public:
		LoadManyPatchFiles(Librarian* librarian, std::shared_ptr<Synth> synth, Array<File> files, std::shared_ptr<AutomaticCategory> automaticCategories)
			: ThreadWithProgressWindow("Loading patch files", true, true), librarian_(librarian), synth_(synth), files_(files), automaticCategories_(automaticCategories)
		{
		}

		void run() {
			int filesDiscovered = files_.size();
			int filesDone = 0;
			for (auto fileChosen : files_) {
				if (threadShouldExit()) {
					return;
				}
				setProgress(filesDone / (double)filesDiscovered);
				auto pathChosen = fileChosen.getFullPathName().toStdString();
				auto newPatches = librarian_->loadSysexPatchesFromDisk(synth_, pathChosen, fileChosen.getFileName().toStdString(), automaticCategories_);
				std::copy(newPatches.begin(), newPatches.end(), std::back_inserter(result_));
				filesDone++;
			}
		}

		std::vector<PatchHolder> result() {
			return result_;
		}

	private:
		Librarian* librarian_;
		std::shared_ptr<Synth> synth_;
		Array<File> files_;
		std::shared_ptr<AutomaticCategory> automaticCategories_;
		std::vector<PatchHolder> result_;
	};

	void Librarian::updateLastPath(std::string& lastPathVariable, std::string const& settingsKey) {
		// Read from settings
		lastPathVariable = Settings::instance().get(settingsKey, "");
		if (lastPathVariable.empty()) {
			// Default directory
			lastPathVariable = File::getSpecialLocation(File::userDocumentsDirectory).getFullPathName().toStdString();
		}
	}

	std::vector<PatchHolder> Librarian::loadSysexPatchesFromDisk(std::shared_ptr<Synth> synth, std::shared_ptr<AutomaticCategory> automaticCategories)
	{
		updateLastPath(lastPath_, "lastImportPath");

		std::string standardFileExtensions = "*.syx;*.mid;*.zip;*.txt;*.json";
		auto legacyLoader = midikraft::Capability::hasCapability<LegacyLoaderCapability>(synth);
		if (legacyLoader) {
			standardFileExtensions += ";" + legacyLoader->additionalFileExtensions();
		}

		FileChooser sysexChooser("Please select the sysex or other patch file you want to load...",
			File(lastPath_), standardFileExtensions);
		if (sysexChooser.browseForMultipleFilesToOpen())
		{
			if (sysexChooser.getResults().size() > 0) {
				// The first file is used to store the path for the next operation
				lastPath_ = sysexChooser.getResults()[0].getParentDirectory().getFullPathName().toStdString();
				Settings::instance().set("lastImportPath", lastPath_);
			}

			LoadManyPatchFiles backgroundTask(this, synth, sysexChooser.getResults(), automaticCategories);
			if (backgroundTask.runThread()) {
				auto result = backgroundTask.result();
				// If this was more than one file, replace the source info with a bulk info source
				Time current = Time::getCurrentTime();
				if (sysexChooser.getResults().size() > 1) {
					for (auto& holder : result) {
						auto newSourceInfo = std::make_shared<FromBulkImportSource>(current, holder.sourceInfo());
						holder.setSourceInfo(newSourceInfo);
					}
				}
				return result;
			}
		}
		// Nothing loaded
		return std::vector<PatchHolder>();
	}

	std::vector<PatchHolder> Librarian::loadSysexPatchesFromDisk(std::shared_ptr<Synth> synth, std::string const& fullpath, std::string const& filename, std::shared_ptr<AutomaticCategory> automaticCategories) {
		auto legacyLoader = midikraft::Capability::hasCapability<LegacyLoaderCapability>(synth);
		TPatchVector patches;
		if (legacyLoader && legacyLoader->supportsExtension(fullpath)) {
			File legacyFile = File::createFileWithoutCheckingPath(fullpath);
			if (legacyFile.existsAsFile()) {
				FileInputStream inputStream(legacyFile);
				std::vector<uint8> data((size_t)inputStream.getTotalLength());
				inputStream.read(&data[0], (int)inputStream.getTotalLength()); // 4 GB Limit
				patches = legacyLoader->load(fullpath, data);
			}
		}
		else if (File(fullpath).getFileExtension() == ".json") {
			std::map<std::string, std::shared_ptr<Synth>> synths;
			synths[synth->getName()] = synth;
			return PatchInterchangeFormat::load(synths, fullpath, automaticCategories);
		}
		else {
			auto messagesLoaded = Sysex::loadSysex(fullpath);
			if (synth) {
				patches = synth->loadSysex(messagesLoaded);
			}
		}

		if (patches.empty()) {
			// Bugger - probably the file is for some synth that is correctly not the active one... happens frequently for me
			// Let's try to sniff the synth from the magics given and then try to reload the file with the correct synth
			//auto detectedSynth = sniffSynth(messagesLoaded);
			//if (detectedSynth) {
				// That's better, now try again
				//patches = detectedSynth->loadSysex(messagesLoaded);
			//}
		}

		return createPatchHoldersFromPatchList(synth, patches, MidiBankNumber::invalid(), [fullpath, filename](MidiBankNumber bankNo, MidiProgramNumber programNo) {
			ignoreUnused(bankNo);
			return std::make_shared<FromFileSource>(filename, fullpath, programNo);
			}, automaticCategories);
	}

	std::vector<PatchHolder> Librarian::createPatchHoldersFromPatchList(std::shared_ptr<Synth> synth, TPatchVector const& patches, MidiBankNumber bankNo, std::function<std::shared_ptr<SourceInfo>(MidiBankNumber, MidiProgramNumber)> generateSourceinfo, std::shared_ptr<AutomaticCategory> automaticCategories)
	{
		// Add the meta information
		std::vector<PatchHolder> result;
		int i = 0;
		for (auto const& patch : patches) {
			auto runningPatchNumber = MidiProgramNumber::fromZeroBaseWithBank(bankNo, i);
			auto sourceInfo = generateSourceinfo(bankNo, runningPatchNumber);
			auto patchHolder = PatchHolder(synth, sourceInfo, patch, automaticCategories);

			// If then synth has a stored program number - currently this is tied to the ProgramDumpCapability - use that. 
			// Alternatively, use the running number to just enumerate patches as they come in. 
			auto programDumpCapability = Capability::hasCapability<ProgramDumpCabability>(synth);
            // We cannot use the program dump capability directly here, because there is no guarantee that the patch is stored as a list of MIDI messages. We need to delegate to the synth to do the right thing
            auto storedProgram = synth->numberForPatch(patch);
            if (programDumpCapability && storedProgram.isValid()) {
                patchHolder.setBank(storedProgram.bank());
                patchHolder.setPatchNumber(storedProgram);
                patchHolder.setSourceInfo(generateSourceinfo(bankNo, storedProgram));
            }
			else
			{
				patchHolder.setBank(bankNo);
				patchHolder.setPatchNumber(runningPatchNumber);
			}
			if (patchHolder.name().empty())
			{
				patchHolder.setName(synth->friendlyProgramAndBankName(patchHolder.bankNumber(), patchHolder.patchNumber()));
			}
			result.push_back(patchHolder);
			i++;
		}
		return result;
	}

	std::vector<PatchHolder> Librarian::loadSysexPatchesManualDump(std::shared_ptr<Synth> synth, std::vector<MidiMessage> const& messages, std::shared_ptr<AutomaticCategory> automaticCategories) {
		TPatchVector patches;
		if (synth) {
			patches = synth->loadSysex(messages);
		}
		// Add the meta information
		Time now;
		return createPatchHoldersFromPatchList(synth, patches, MidiBankNumber::invalid(), [now](MidiBankNumber bankNo, MidiProgramNumber programNo) {
			ignoreUnused(programNo);
			return std::make_shared<FromSynthSource>(now, bankNo);
			}, automaticCategories);
	}

	void Librarian::sendBankToSynth(SynthBank const& synthBank, bool fullBank, ProgressHandler* progressHandler, std::function<void(bool completed)> finishedHandler)
	{
		auto synth = synthBank.synth();
		if (!synth) {
			return;
		}

		auto location = midikraft::Capability::hasCapability<midikraft::MidiLocationCapability>(synth);
		if (!location || !location->channel().isValid() /* || !synth->wasDetected()*/) {
			spdlog::warn("Synth {} is currently not detected, please turn on and re-run connectivity check", synth->getName());
			return;
		}

		auto bankSendCapability = midikraft::Capability::hasCapability<BankSendCapability>(synth);
		auto editBufferCapability = midikraft::Capability::hasCapability<EditBufferCapability>(synth);
		auto programDumpCapability = midikraft::Capability::hasCapability<ProgramDumpCabability>(synth);
		if (bankSendCapability && (editBufferCapability || programDumpCapability)) {
			// We use the BankSendCapability to create one or messages to transport all patches
			std::vector<std::vector<MidiMessage>> patchMessages;
			int i = 0; 
			for (auto &patch: synthBank.patches()) {
				if (programDumpCapability) {
					patchMessages.push_back(programDumpCapability->patchToProgramDumpSysex(patch.patch(), MidiProgramNumber::fromZeroBase(i)));
					i++;
				}
				else if (editBufferCapability) {
					patchMessages.push_back(editBufferCapability->patchToSysex(patch.patch()));
					i++;
				}
			}

			auto messages = bankSendCapability->createBankMessages(patchMessages);
			synth->sendBlockOfMessagesToSynth(location->midiOutput(), messages);
			if (finishedHandler) {
				finishedHandler(true);
			}

		} else if (programDumpCapability) {
			// Count how many to send first
			int count = 0;
			int i = 0;
			for (auto const& patch : synthBank.patches()) {
				ignoreUnused(patch);
				if (fullBank || synthBank.isPositionDirty(i++)) {
					count++;
				}
			}

			// Now to send and update the progressHandler
			int sent = 0;
			i = 0;
			for (auto const& patch : synthBank.patches()) {
				if (progressHandler) progressHandler->setMessage(fmt::format("Sending patch {} to {}", patch.name(), synth->friendlyProgramName(patch.patchNumber())));
				if (fullBank || synthBank.isPositionDirty(i++)) {
					auto messages = programDumpCapability->patchToProgramDumpSysex(patch.patch(), patch.patchNumber());
					synth->sendBlockOfMessagesToSynth(location->midiOutput(), messages);
				}
				if (progressHandler) {
					progressHandler->setMessage(fmt::format("Sending patch #{}: '{}'...", sent, patch.name()));
					progressHandler->setProgressPercentage(++sent / (double)count);
				}
				if (progressHandler && progressHandler->shouldAbort()) {
					spdlog::warn("Canceled bank upload in mid-flight!");
					if (finishedHandler) {
						finishedHandler(false);
					}
					return;
				}
			}
			if (finishedHandler) {
				finishedHandler(true);
			}
		}
		else {
			spdlog::warn("Sending banks to {} is not implemented yet", synth->getName());
		}
	}

	class ExportSysexFilesInBackground : public ThreadWithProgressWindow {
	public:
		ExportSysexFilesInBackground(String title, File dest, Librarian::ExportParameters _params, std::vector<PatchHolder> const& _patches) : ThreadWithProgressWindow(title, true, false),
			destination(dest), params(_params), patches(_patches)
		{}

		virtual void run() override
		{
			if (destination.existsAsFile()) {
				destination.deleteFile();
			}
			else if (destination.exists() && params.fileOption != Librarian::MANY_FILES) {
				// This is a directory, but we didn't want one
				spdlog::warn("Can't overwrite a directory, please choose a different name!");
				return;
			}

			// Create a temporary directory to build the result
			TemporaryDirectory tempDir("KnobKraftOrm", "sysex_export_tmp");
			ZipFile::Builder builder;
			std::vector<MidiMessage> allMessages;

			if (params.formatOption == Librarian::BANK_DUMP && patches.size() > 0) 
			{
				auto bsc = Capability::hasCapability<BankSendCapability>(patches[0].synth());
				auto pdc = Capability::hasCapability<ProgramDumpCabability>(patches[0].synth());
				auto ebc = Capability::hasCapability<EditBufferCapability>(patches[0].synth());
				std::vector<std::vector<MidiMessage>> patchMessages;
				int i = 0;
				for (const auto& patch : patches) {
					if (pdc) {
						patchMessages.push_back(pdc->patchToProgramDumpSysex(patch.patch(), MidiProgramNumber::fromZeroBase(i)));
						i++;
					}
					else if (ebc) {
						patchMessages.push_back(ebc->patchToSysex(patch.patch()));
					}
				}
				allMessages = bsc->createBankMessages(patchMessages);
			}
			else {
				// Now, iterate over the list of patches and pack them one by one into the zip file!		
				int count = 0;
				for (const auto& patch : patches) {
					if (patch.patch()) {
						std::vector<MidiMessage> sysexMessages;
						switch (params.formatOption) {
						case Librarian::PROGRAM_DUMPS:
						{
							// Let's see if we have program dump capability for the synth!
							auto pdc = Capability::hasCapability<ProgramDumpCabability>(patch.synth());
							if (pdc) {
								sysexMessages = pdc->patchToProgramDumpSysex(patch.patch(), patch.patchNumber());
								break;
							}
						}
						// fall through do default then
						default:
						case Librarian::EDIT_BUFFER_DUMPS:
							// Every synth is forced to have an implementation for this
							sysexMessages = patch.synth()->dataFileToSysex(patch.patch(), nullptr);
							break;
						}

						String fileName = patch.name();
						switch (params.fileOption) {
						case Librarian::MANY_FILES:
						{
							std::string result = Sysex::saveSysexIntoNewFile(destination.getFullPathName().toStdString(), File::createLegalFileName(fileName.trim()).toStdString(), sysexMessages);
							break;
						}
						case Librarian::ZIPPED_FILES:
						{
							std::string result = Sysex::saveSysexIntoNewFile(tempDir.name(), File::createLegalFileName(fileName.trim()).toStdString(), sysexMessages);
							builder.addFile(File(result), 6);
							break;
						}
						case Librarian::MID_FILE:
						case Librarian::ONE_FILE:
						{
							std::copy(sysexMessages.begin(), sysexMessages.end(), std::back_inserter(allMessages));
							break;
						}
						}
					}
					setProgress(count++ / (double)patches.size());
					if (threadShouldExit()) {
						break;
					}
				}
			}
			switch (params.fileOption)
			{
			case Librarian::ZIPPED_FILES:
			{
				FileOutputStream targetStream(destination);
				builder.writeToStream(targetStream, nullptr);
				break;
			}
			case Librarian::ONE_FILE:
			{
				Sysex::saveSysex(destination.getFullPathName().toStdString(), allMessages);
				break;
			}
			case Librarian::MID_FILE:
			{
				MidiFile midiFile;
				MidiMessageSequence mmSeq;
				for (const auto& msg : allMessages) {
					mmSeq.addEvent(msg, 0.0);
				}

				// Add to track 1 of MIDI file
				midiFile.addTrack(mmSeq);
				midiFile.setTicksPerQuarterNote(96);

				// Done, write to file
				File file(destination);
				if (file.existsAsFile()) {
					file.deleteFile();
				}
				FileOutputStream stream(file);
				if (!midiFile.writeTo(stream, 1)) {
					spdlog::error("Failed to write SMF file to {}", destination.getFullPathName());
				}
				stream.flush();
			}
			default:
				// Nothing to do
				break;
			}
		}

	private:
		File destination;
		Librarian::ExportParameters params;
		std::vector<PatchHolder> const& patches;
	};

	void Librarian::saveSysexPatchesToDisk(ExportParameters params, std::vector<PatchHolder> const& patches)
	{
		File destination;
		switch (params.fileOption) {
		case MANY_FILES:
		{
			updateLastPath(lastExportDirectory_, "lastExportDirectory");
			FileChooser sysexChooser("Please choose a directory for the files that will be created", File(lastExportDirectory_));
			if (!sysexChooser.browseForDirectory()) {
				return;
			}
			destination = sysexChooser.getResult();
			Settings::instance().set("lastExportDirectory", destination.getFullPathName().toStdString());
			break;
		}
		case ZIPPED_FILES:
		{
			updateLastPath(lastExportZipFilename_, "lastExportZipFilename");
			FileChooser sysexChooser("Please enter the name of the zip file to create...", File(lastExportZipFilename_), "*.zip");
			if (!sysexChooser.browseForFileToSave(true)) {
				return;
			}
			destination = sysexChooser.getResult();
			Settings::instance().set("lastExportZipFilename", destination.getFullPathName().toStdString());
			break;
		}
		case ONE_FILE:
		{
			updateLastPath(lastExportSyxFilename_, "lastExportSyxFilename");
			FileChooser sysexChooser("Please enter the name of the syx file to create...", File(lastExportSyxFilename_), "*.syx");
			if (!sysexChooser.browseForFileToSave(true)) {
				return;
			}
			destination = sysexChooser.getResult();
			Settings::instance().set("lastExportSyxFilename", destination.getFullPathName().toStdString());
			break;
		}
		case MID_FILE:
		{
			updateLastPath(lastExportMidFilename_, "lastExportMidFilename");
			FileChooser sysexChooser("Please enter the name of the MIDI file to create...", File(lastExportSyxFilename_), "*.mid");
			if (!sysexChooser.browseForFileToSave(true)) {
				return;
			}
			destination = sysexChooser.getResult();
			Settings::instance().set("lastExportMidFilename", destination.getFullPathName().toStdString());
		}
		}

		// This is actually synchronous, we just launch it in a thread so the progress UI will still update.
		ExportSysexFilesInBackground progressWindow("Exporting...", destination, params, patches);

		if (progressWindow.runThread()) {
			// Done, now just wrap up
			switch (params.fileOption) {
			case MANY_FILES:
				// Nothing todo, just display success
				AlertWindow::showMessageBox(AlertWindow::InfoIcon, "Patches exported",
					fmt::format("All {} patches selected have been exported into the following directory:\n\n{}\n\nThese files can be re-imported into another KnobKraft Orm instance or else\n"
						"the patches can be sent into the synth with a sysex tool", patches.size(), destination.getFullPathName().toStdString()));
				break;
			case ZIPPED_FILES: {
				AlertWindow::showMessageBox(AlertWindow::InfoIcon, "Patches exported",
					fmt::format("All {} patches selected have been exported into the following: ZIP file:\n\n{}\n\nThis file can be re-imported into another KnobKraft Orm instance or else\n"
						"the patches can be sent into the synth with a sysex tool", patches.size(), destination.getFullPathName().toStdString()));
				break;
			}
			case MID_FILE:
			case ONE_FILE:
			{
				AlertWindow::showMessageBox(AlertWindow::InfoIcon, "Patches exported",
					fmt::format("All {} patches selected have been exported into the following file:\n\n{}\n\nThis file can be re-imported into another KnobKraft Orm instance or else\n"
						"the patches can be sent into the synth with a sysex tool", patches.size(), destination.getFullPathName().toStdString()));
				break;
			}
			}
		}
	}

	void Librarian::clearHandlers()
	{
		// This is to clear up any remaining MIDI callback handlers, e.g. on User canceling an operation
		while (!handles_.empty()) {
			auto handle = handles_.top();
			handles_.pop();
			MidiController::instance()->removeMessageHandler(handle);
		}
	}

	void Librarian::startDownloadNextEditBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, bool sendProgramChange) {
		// Get all commands
		std::vector<MidiMessage> messages;
		auto editBufferCapability = midikraft::Capability::hasCapability<EditBufferCapability>(synth);
		if (editBufferCapability) {
			currentEditBuffer_.clear();
			auto midiLocation = midikraft::Capability::hasCapability<MidiLocationCapability>(synth);
			if (midiLocation) {
				if (sendProgramChange) {
					messages.push_back(MidiMessage::programChange(midiLocation->channel().toOneBasedInt(), downloadNumber_));
				}
				auto requestMessages = editBufferCapability->requestEditBufferDump();
				std::copy(requestMessages.cbegin(), requestMessages.cend(), std::back_inserter(messages));
			}
			else {
				spdlog::error("Can't send to synth because no MIDI location implemented for it");
			}
		}
		else {
			spdlog::error("Failure: This synth does not implement any valid capability to start downloading a full bank");
			downloadNumber_ = endDownloadNumber_;
		}

		// Send messages
		if (!messages.empty()) {
			synth->sendBlockOfMessagesToSynth(midiOutput->deviceInfo(), messages);
		}
	}

	void Librarian::startDownloadNextPatch(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth) {
		// Get all commands
		std::vector<MidiMessage> messages;
		auto programDumpCapability = midikraft::Capability::hasCapability<ProgramDumpCabability>(synth);
		if (programDumpCapability) {
			currentProgramDump_.clear();
			messages = programDumpCapability->requestPatch(downloadNumber_);
		}
		else {
			spdlog::error("Failure: This synth does not implement any valid capability to start downloading a full bank");
			downloadNumber_ = endDownloadNumber_;
		}

		// Send messages
		if (!messages.empty()) {
			synth->sendBlockOfMessagesToSynth(midiOutput->deviceInfo(), messages);
		}
	}

	void Librarian::startDownloadNextDataItem(std::shared_ptr<SafeMidiOutput> midiOutput, DataFileLoadCapability* sequencer, int dataFileIdentifier) {
		std::vector<MidiMessage> request = sequencer->requestDataItem(downloadNumber_, dataFileIdentifier);
		// If this is a synth, it has a throttled send method
		auto synth = dynamic_cast<Synth*>(sequencer);
		if (synth) {
			synth->sendBlockOfMessagesToSynth(midiOutput->deviceInfo(), request);
		}
		else {
			// This is not a synth... fall back to old behavior
			midiOutput->sendBlockOfMessagesFullSpeed(request);
		}
	}

	void Librarian::handleNextStreamPart(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler* progressHandler, const juce::MidiMessage& message, StreamLoadCapability::StreamType streamType)
	{
		auto streamLoading = midikraft::Capability::hasCapability<StreamLoadCapability>(synth);
		if (streamLoading) {
			if (streamLoading->isMessagePartOfStream(message, streamType)) {
				currentDownload_.push_back(message);
				int progressTotal = streamLoading->numberOfStreamMessagesExpected(streamType);
				if (progressTotal > 0 && progressHandler) {
					progressHandler->setProgressPercentage(currentDownload_.size() / (double)progressTotal);
				}
				if (streamLoading->isStreamComplete(currentDownload_, streamType)) {
					clearHandlers();
					auto result = synth->loadSysex(currentDownload_);
					onFinished_(tagPatchesWithImportFromSynth(synth, result, currentDownloadBank_));
					if (progressHandler) progressHandler->onSuccess();
				}
				else if (progressHandler && progressHandler->shouldAbort()) {
					clearHandlers();
					progressHandler->onCancel();
				}
				else if (streamLoading->shouldStreamAdvance(currentDownload_, streamType)) {
					downloadNumber_++;
					auto messages = streamLoading->requestStreamElement(downloadNumber_, streamType);
					synth->sendBlockOfMessagesToSynth(midiOutput->deviceInfo(), messages);
					if (progressTotal == -1 && progressHandler) progressHandler->setProgressPercentage(downloadNumber_ / (double)expectedDownloadNumber_);
				}
			}
		}
		else {
			jassertfalse;
		}
	}

	void Librarian::handleNextEditBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler* progressHandler, const juce::MidiMessage& editBuffer, MidiBankNumber bankNo) {
		auto editBufferCapability = midikraft::Capability::hasCapability<EditBufferCapability>(synth);
		// This message might be a part of a multi-message program dump?
		if (editBufferCapability) {
			auto handshake = editBufferCapability->isMessagePartOfEditBuffer(editBuffer);
			if (handshake.isPartOfEditBufferDump) {
				// See if we should send a reply (ACK)
				if (!handshake.handshakeReply.empty()) {
					synth->sendBlockOfMessagesToSynth(midiOutput->deviceInfo(), handshake.handshakeReply);
				}
				currentEditBuffer_.push_back(editBuffer);
				if (editBufferCapability->isEditBufferDump(currentEditBuffer_)) {
					// Ok, that worked, save it and continue!
					std::copy(currentEditBuffer_.begin(), currentEditBuffer_.end(), std::back_inserter(currentDownload_));

					// Finished?
					if (downloadNumber_ >= endDownloadNumber_ - 1) {
						clearHandlers();
						auto patches = synth->loadSysex(currentDownload_);
						onFinished_(tagPatchesWithImportFromSynth(synth, patches, bankNo));
						if (progressHandler) progressHandler->onSuccess();
					}
					else if (progressHandler->shouldAbort()) {
						clearHandlers();
						if (progressHandler) progressHandler->onCancel();
					}
					else {
						downloadNumber_++;
						startDownloadNextEditBuffer(midiOutput, synth, true); // To continue with more than one download makes only sense if we send program change commands
						if (progressHandler) progressHandler->setProgressPercentage((downloadNumber_ - startDownloadNumber_) / (double)(endDownloadNumber_ - startDownloadNumber_));
					}
				}
			}
		}
		else {
			// Ignore message
		}
	}

	void Librarian::handleNextProgramBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler* progressHandler, const juce::MidiMessage& editBuffer, MidiBankNumber bankNo) {
		auto programDumpCapability = midikraft::Capability::hasCapability<ProgramDumpCabability>(synth);
		// This message might be a part of a multi-message program dump?
		if (programDumpCapability) {
			auto handshake = programDumpCapability->isMessagePartOfProgramDump(editBuffer);
			if (handshake.isPartOfProgramDump) {
				currentProgramDump_.push_back(editBuffer);
				// See if we should send a reply (ACK)
				if (!handshake.handshakeReply.empty()) {
					synth->sendBlockOfMessagesToSynth(midiOutput->deviceInfo(), handshake.handshakeReply);
				}
				if (programDumpCapability->isSingleProgramDump(currentProgramDump_)) {
					// Ok, that worked, save it and continue!
					std::copy(currentProgramDump_.begin(), currentProgramDump_.end(), std::back_inserter(currentDownload_));

					// Finished?
					if (downloadNumber_ >= endDownloadNumber_ - 1) {
						clearHandlers();
						auto patches = synth->loadSysex(currentDownload_);
						onFinished_(tagPatchesWithImportFromSynth(synth, patches, bankNo));
						if (progressHandler) progressHandler->onSuccess();
					}
					else if (progressHandler->shouldAbort()) {
						clearHandlers();
						if (progressHandler) progressHandler->onCancel();
					}
					else {
						downloadNumber_++;
						startDownloadNextPatch(midiOutput, synth);
						if (progressHandler) progressHandler->setProgressPercentage((downloadNumber_ - startDownloadNumber_) / (double)(endDownloadNumber_ - startDownloadNumber_));
					}
				}
			}
		}
	}

	void Librarian::handleNextBankDump(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler* progressHandler, const juce::MidiMessage& bankDump, MidiBankNumber bankNo)
	{
		ignoreUnused(midiOutput); //TODO why?
		auto bankDumpCapability = midikraft::Capability::hasCapability<BankDumpCapability>(synth);
		if (bankDumpCapability && bankDumpCapability->isBankDump(bankDump)) {
			currentDownload_.push_back(bankDump);
			if (bankDumpCapability->isBankDumpFinished(currentDownload_)) {
				clearHandlers();
				auto patches = synth->loadSysex(currentDownload_);
				onFinished_(tagPatchesWithImportFromSynth(synth, patches, bankNo));
				progressHandler->onSuccess();
			}
			else if (progressHandler->shouldAbort()) {
				clearHandlers();
				progressHandler->onCancel();
			}
			else {
				progressHandler->setProgressPercentage(currentDownload_.size() / (double)(expectedDownloadNumber_));
			}
		}
	}

	std::vector<PatchHolder> Librarian::tagPatchesWithImportFromSynth(std::shared_ptr<Synth> synth, TPatchVector& patches, MidiBankNumber bankNo) {
		std::vector<PatchHolder> result;
		auto now = Time::getCurrentTime();
		return createPatchHoldersFromPatchList(synth, patches, bankNo, [now](MidiBankNumber bank, MidiProgramNumber programNumber) {
			ignoreUnused(programNumber);
			return std::make_shared<FromSynthSource>(now, bank);
			}, nullptr);
	}

	void Librarian::tagPatchesWithMultiBulkImport(std::vector<PatchHolder>& patches) {
		// We have multiple import sources, so we need to modify the SourceInfo in the patches with a BulkImport info
		Time now = Time::getCurrentTime();
		for (auto& patch : patches) {
			auto bulkInfo = std::make_shared<FromBulkImportSource>(now, patch.sourceInfo());
			patch.setSourceInfo(bulkInfo);
		}
	}

}
