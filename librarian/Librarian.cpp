/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Librarian.h"

#include "Synth.h"
#include "StepSequencer.h"
#include "Sysex.h"
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

#include <boost/format.hpp>
#include <set>
#include "Settings.h"

namespace midikraft {


	void Librarian::startDownloadingAllPatches(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, std::vector<MidiBankNumber> bankNo,
		ProgressHandler *progressHandler, TFinishedHandler onFinished) {

		downloadBankNumber_ = 0;
		if (!bankNo.empty()) {
			currentDownloadedPatches_.clear();
			nextBankHandler_ = [this, midiOutput, synth, progressHandler, bankNo, onFinished](std::vector<midikraft::PatchHolder> patchesLoaded) {
				std::copy(patchesLoaded.begin(), patchesLoaded.end(), std::back_inserter(currentDownloadedPatches_));
				downloadBankNumber_++;
				if (downloadBankNumber_ == bankNo.size()) {
					if (bankNo.size() > 1) {
						tagPatchesWithMultiBulkImport(currentDownloadedPatches_);
					}
					onFinished(currentDownloadedPatches_);
				}
				else {
					if (!progressHandler->shouldAbort()) {
						progressHandler->setMessage((boost::format("Importing %s from %s...") % synth->friendlyBankName(bankNo[downloadBankNumber_]) % synth->getName()).str());
						startDownloadingAllPatches(midiOutput, synth, bankNo[downloadBankNumber_], progressHandler, nextBankHandler_);
					}
				}
			};
			progressHandler->setMessage((boost::format("Importing %s from %s...") % synth->friendlyBankName(bankNo[0]) % synth->getName()).str());
			startDownloadingAllPatches(midiOutput, synth, bankNo[0], progressHandler, nextBankHandler_);
		}
	}

	void Librarian::startDownloadingAllPatches(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, MidiBankNumber bankNo,
		ProgressHandler *progressHandler, TFinishedHandler onFinished)
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
		auto streamLoading = midikraft::Capability::hasCapability<StreamLoadCapability>(synth);
		auto bankCapableSynth = midikraft::Capability::hasCapability<BankDumpCapability>(synth);
		auto handshakeLoadingRequired = midikraft::Capability::hasCapability<HandshakeLoadingCapability>(synth);
		if (streamLoading) {
			// Simple enough, we hope
			MidiController::instance()->addMessageHandler(handle, [this, synth, progressHandler, midiOutput](MidiInput *source, const juce::MidiMessage &editBuffer) {
				ignoreUnused(source);
				this->handleNextStreamPart(midiOutput, synth, progressHandler, editBuffer, StreamLoadCapability::StreamType::BANK_DUMP);
			});
			handles_.push(handle);
			currentDownload_.clear();
			auto messages = streamLoading->requestStreamElement(0, StreamLoadCapability::StreamType::BANK_DUMP);
			synth->sendBlockOfMessagesToSynth(midiOutput->name(), MidiHelpers::bufferFromMessages(messages));
		}
		else if (handshakeLoadingRequired) {
			// These are proper protocols that are implemented - each message we get from the synth has to be answered by an appropriate next message
			std::shared_ptr<HandshakeLoadingCapability::ProtocolState>  state = handshakeLoadingRequired->createStateObject();
			if (state) {
				MidiController::instance()->addMessageHandler(handle, [this, handshakeLoadingRequired, state, progressHandler, midiOutput, synth, bankNo](MidiInput *source, const juce::MidiMessage &protocolMessage) {
					ignoreUnused(source);
					std::vector<MidiMessage> answer;
					if (handshakeLoadingRequired->isNextMessage(protocolMessage, answer, state)) {
						// Store message
						currentDownload_.push_back(protocolMessage);
					}
					// Send an answer if the handshake handler constructed one
					if (!answer.empty()) {
						auto buffer = MidiHelpers::bufferFromMessages(answer);
						synth->sendBlockOfMessagesToSynth(midiOutput->name(), buffer);
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
		}
		else if (bankCapableSynth) {
			// This is a mixture - you send one message (bank request), and then you get either one message back (like Kawai K3) or a stream of messages with
			// one message per patch (e.g. Access Virus or Matrix1000)
			MidiBuffer buffer = MidiHelpers::bufferFromMessages(bankCapableSynth->requestBankDump(bankNo));
			std::string outname = midiOutput->name();
			RunWithRetry::start([this, synth, outname, buffer]() {
					synth->sendBlockOfMessagesToSynth(outname, buffer);
					}, 
				[this]() {
					return currentDownload_.empty();
				},
				3,
				500,
				"initiating bank dump");

			MidiController::instance()->addMessageHandler(handle, [this, synth, progressHandler, midiOutput, bankNo](MidiInput *source, const juce::MidiMessage &editBuffer) {
				ignoreUnused(source);
				this->handleNextBankDump(midiOutput, synth, progressHandler, editBuffer, bankNo);
			});
			handles_.push(handle);
			currentDownload_.clear();
		}
		else {
			// Uh, stone age, need to start a loop
			MidiController::instance()->addMessageHandler(handle, [this, synth, progressHandler, midiOutput, bankNo](MidiInput *source, const juce::MidiMessage &editBuffer) {
				ignoreUnused(source);
				this->handleNextEditBuffer(midiOutput, synth, progressHandler, editBuffer, bankNo);
			});
			handles_.push(handle);
			downloadNumber_ = bankNo.toZeroBased() * synth->numberOfPatches();
			startDownloadNumber_ = downloadNumber_;
			endDownloadNumber_ = downloadNumber_ + synth->numberOfPatches() - 1;
			startDownloadNextPatch(midiOutput, synth);
		}
	}

	void Librarian::downloadEditBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, TFinishedHandler onFinished)
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
			MidiController::instance()->addMessageHandler(handle, [this, synth, progressHandler, midiOutput](MidiInput *source, const juce::MidiMessage &editBuffer) {
				ignoreUnused(source);
				this->handleNextStreamPart(midiOutput, synth, progressHandler, editBuffer, StreamLoadCapability::StreamType::EDIT_BUFFER_DUMP);
			});
			handles_.push(handle);
			currentDownload_.clear();
			auto messages = streamLoading->requestStreamElement(0, StreamLoadCapability::StreamType::EDIT_BUFFER_DUMP);
			synth->sendBlockOfMessagesToSynth(midiOutput->name(), MidiHelpers::bufferFromMessages(messages));
		} else if (editBufferCapability) {
			// Uh, stone age, need to start a loop
			MidiController::instance()->addMessageHandler(handle, [this, synth, progressHandler, midiOutput](MidiInput *source, const juce::MidiMessage &editBuffer) {
				ignoreUnused(source);
				this->handleNextEditBuffer(midiOutput, synth, progressHandler, editBuffer, MidiBankNumber::fromZeroBase(0));
			});
			handles_.push(handle);
			// Special case - load only a single patch. In this case we're interested in the edit buffer only!
			startDownloadNumber_ = 0;
			endDownloadNumber_ = 0;
			auto message = editBufferCapability->requestEditBufferDump();
			midiOutput->sendMessageNow(message);
		}
		else if (programDumpCapability && programChangeCapability) {
			auto messages = programDumpCapability->requestPatch(programChangeCapability->lastProgramChange().toZeroBased());
			synth->sendBlockOfMessagesToSynth(midiOutput->name(), MidiHelpers::bufferFromMessages(messages));
		}
		else {
			SimpleLogger::instance()->postMessage("The " + synth->getName() + " has no way to request the edit buffer or program place");
		}
	}

	void Librarian::startDownloadingSequencerData(std::shared_ptr<SafeMidiOutput> midiOutput, DataFileLoadCapability *sequencer, int dataFileIdentifier, ProgressHandler *progressHandler, TStepSequencerFinishedHandler onFinished)
	{
		// First things first - this should not be called more than once at a time, and there should be no other Librarian callback handlers be registered!
		jassert(handles_.empty());
		clearHandlers();

		downloadNumber_ = 0;
		currentDownload_.clear();
		onSequencerFinished_ = onFinished;

		auto handle = MidiController::makeOneHandle();
		MidiController::instance()->addMessageHandler(handle, [this, sequencer, progressHandler, midiOutput, dataFileIdentifier](MidiInput *source, const MidiMessage &message) {
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

	Synth *Librarian::sniffSynth(std::vector<MidiMessage> const &messages) const
	{
		std::set<Synth *> result;
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
		LoadManyPatchFiles(Librarian *librarian, std::shared_ptr<Synth> synth, Array<File> files, std::shared_ptr<AutomaticCategory> automaticCategories) 
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
		Librarian *librarian_;
		std::shared_ptr<Synth> synth_;
		Array<File> files_;
		std::shared_ptr<AutomaticCategory> automaticCategories_;
		std::vector<PatchHolder> result_;
	};

	void Librarian::updateLastPath() {
		if (lastPath_.empty()) {
			// Read from settings
			lastPath_ = Settings::instance().get("lastImportPath", "");
			if (lastPath_.empty()) {
				// Default directory
				lastPath_ = File::getSpecialLocation(File::userDocumentsDirectory).getFullPathName().toStdString();
			}
		}
	}

	std::vector<PatchHolder> Librarian::loadSysexPatchesFromDisk(std::shared_ptr<Synth> synth, std::shared_ptr<AutomaticCategory> automaticCategories)
	{
		updateLastPath();

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
					for (auto &holder : result) {
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

	std::vector<PatchHolder> Librarian::loadSysexPatchesFromDisk(std::shared_ptr<Synth> synth, std::string const &fullpath, std::string const &filename, std::shared_ptr<AutomaticCategory> automaticCategories) {
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
			return PatchInterchangeFormat::load(synth, fullpath, automaticCategories);
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

		// Add the meta information
		std::vector<PatchHolder> result;
		int i = 0;
		for (auto patch : patches) {
			result.push_back(PatchHolder(synth, std::make_shared<FromFileSource>(filename, fullpath, MidiProgramNumber::fromZeroBase(i)), patch, MidiProgramNumber::fromZeroBase(i), automaticCategories));
			i++;
		}
		return result;
	}

	class ProgressWindow: public ThreadWithProgressWindow {
	public:
		ProgressWindow(String title, double *progress) : ThreadWithProgressWindow(title, true, false), progress_(progress) {}

		virtual void run() override
		{
			while (!threadShouldExit() && *progress_ < 1.0) {
				setProgress(*progress_);
				Thread::sleep(50);
			}
		}	

	private:
		double *progress_;
	};

	void Librarian::saveSysexPatchesToDisk(std::vector<PatchHolder> const &patches)
	{
		updateLastPath();
		FileChooser sysexChooser("Please enter the name of the zip file to create...", File(lastPath_), ".zip");
		if (sysexChooser.browseForFileToSave(true)) {
			double progress = 0.0;
			ProgressWindow progressWindow("Compressing ZIP File", &progress);
			progressWindow.launchThread();

			File zipFile = sysexChooser.getResult();
			lastPath_ = zipFile.getFullPathName().toStdString();
			Settings::instance().set("lastImportPath", lastPath_);
			if (zipFile.existsAsFile()) {
				zipFile.deleteFile();
			}
			else if (zipFile.exists()) {
				// This is a directory
				SimpleLogger::instance()->postMessage("Can't overwrite a directory, please choose a different name!");
				return;
			}

			// Create a temporary directory to build the result
			TemporaryDirectory tempDir;

			// Now, iterate over the list of patches and pack them one by one into the zip file!
			ZipFile::Builder builder;
			for (auto patch : patches) {
				if (patch.patch()) {
					auto sysexMessages = patch.synth()->patchToSysex(patch.patch(), nullptr);
					String fileName = patch.name();
					std::string result = Sysex::saveSysexIntoNewFile(tempDir.name(), File::createLegalFileName(fileName.trim()).toStdString(), sysexMessages);
					//TODO what a peculiar return type
					if (result != "Failure") {
						builder.addFile(File(result), 6);
					}
					else {
						jassertfalse;
					}
				}
				else {
					jassertfalse;
				}
			}

			FileOutputStream targetStream(zipFile);			
			builder.writeToStream(targetStream, &progress);
			progressWindow.stopThread(200);
			AlertWindow::showMessageBox(AlertWindow::InfoIcon, "Patches exported",
				(boost::format("All %d patches selected have been exported into the following: ZIP file:\n\n%s\n\nThis file can be re-imported into another KnobKraft Orm instance or else\n"
					"the patches can be sent into the edit buffer of the synth with a sysex tool") % patches.size() % zipFile.getFullPathName().toStdString()).str());
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

	void Librarian::startDownloadNextPatch(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth) {
		auto editBufferCapability = midikraft::Capability::hasCapability<EditBufferCapability>(synth);
		auto programDumpCapability = midikraft::Capability::hasCapability<ProgramDumpCabability>(synth);

		// Get all commands
		std::vector<MidiMessage> messages;
		if (programDumpCapability) {
			messages = programDumpCapability->requestPatch(downloadNumber_);
		}
		else if (editBufferCapability) {
			//messages.push_back(MidiMessage::controllerEvent(synth->channel().toOneBasedInt(), 32, bankNo));
			auto midiLocation = midikraft::Capability::hasCapability<MidiLocationCapability>(synth);
			assert(midiLocation);
			if (midiLocation) {
				messages.push_back(MidiMessage::programChange(midiLocation->channel().toOneBasedInt(), downloadNumber_));
				messages.push_back(editBufferCapability->requestEditBufferDump());
			}
		}
		else {
			SimpleLogger::instance()->postMessage("Failure: This synth does not implement any valid capability to start downloading a full bank");
			downloadNumber_ = endDownloadNumber_; 
		}

		// Send messages
		if (!messages.empty()) {
			auto buffer = MidiHelpers::bufferFromMessages(messages);
			synth->sendBlockOfMessagesToSynth(midiOutput->name(), buffer);
		}
	}

	void Librarian::startDownloadNextDataItem(std::shared_ptr<SafeMidiOutput> midiOutput, DataFileLoadCapability *sequencer, int dataFileIdentifier) {
		std::vector<MidiMessage> request = sequencer->requestDataItem(downloadNumber_, dataFileIdentifier);
		auto buffer = MidiHelpers::bufferFromMessages(request);
		// If this is a synth, it has a throttled send method
		auto synth = dynamic_cast<Synth *>(sequencer);
		if (synth) {
			synth->sendBlockOfMessagesToSynth(midiOutput->name(), buffer);
		}
		else {
			// This is not a synth... fall back to old behavior
			midiOutput->sendBlockOfMessagesFullSpeed(buffer);
		}
	}

	void Librarian::handleNextStreamPart(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, const juce::MidiMessage &message, StreamLoadCapability::StreamType streamType)
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
					onFinished_(tagPatchesWithImportFromSynth(synth, result, MidiBankNumber::fromZeroBase(0)));
					if (progressHandler) progressHandler->onSuccess();
				}
				else if (progressHandler && progressHandler->shouldAbort()) {
					clearHandlers();
					progressHandler->onCancel();
				}
				else if (streamLoading->shouldStreamAdvance(currentDownload_, streamType)) {
					downloadNumber_++;
					auto messages = streamLoading->requestStreamElement(downloadNumber_, streamType);
					synth->sendBlockOfMessagesToSynth(midiOutput->name(), MidiHelpers::bufferFromMessages(messages));
					if (progressTotal == -1 && progressHandler) progressHandler->setProgressPercentage(downloadNumber_ / (double)synth->numberOfPatches());
				}
			}
		}
		else {
			jassertfalse;
		}
	}

	void Librarian::handleNextEditBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, const juce::MidiMessage &editBuffer, MidiBankNumber bankNo) {
		auto editBufferCapability = midikraft::Capability::hasCapability<EditBufferCapability>(synth);
		auto programDumpCapability = midikraft::Capability::hasCapability<ProgramDumpCabability>(synth);
		if ((editBufferCapability  && editBufferCapability->isEditBufferDump(editBuffer)) ||
			(programDumpCapability && programDumpCapability->isSingleProgramDump(editBuffer))) {
			// Ok, that worked, save it and continue!
			currentDownload_.push_back(MidiMessage(editBuffer));

			// Finished?
			if (downloadNumber_ >= endDownloadNumber_) {
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
				if (progressHandler) progressHandler->setProgressPercentage((downloadNumber_ - startDownloadNumber_) / (double)synth->numberOfPatches());
			}
		}
		else {
			// Ignore message
			//TODO we could add an echo check here, as e.g. the MKS80 is prone to just echo out everything you sent to it, you'd end up here.
			/*jassert(false);*/
		}
	}

	void Librarian::handleNextBankDump(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, const juce::MidiMessage &bankDump, MidiBankNumber bankNo)
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
				progressHandler->setProgressPercentage(currentDownload_.size() / (double)synth->numberOfPatches());
			}
		}
	}

	std::vector<PatchHolder> Librarian::tagPatchesWithImportFromSynth(std::shared_ptr<Synth> synth, TPatchVector &patches, MidiBankNumber bankNo) {
		std::vector<PatchHolder> result;
		auto now = Time::getCurrentTime();
		//TODO - this probably should have been handed down from the main program?
		auto detector = std::make_shared<midikraft::AutomaticCategory>();
		int i = 0;
		for (auto patch : patches) {
			MidiProgramNumber place = MidiProgramNumber::fromZeroBase(i++);
			auto realpatch = std::dynamic_pointer_cast<Patch>(patch);
			if (realpatch) {
				place = realpatch->patchNumber();
			}
			result.push_back(PatchHolder(synth, std::make_shared<FromSynthSource>(now, bankNo), patch, place, detector));
		}
		return result;
	}

	void Librarian::tagPatchesWithMultiBulkImport(std::vector<PatchHolder> &patches) {
		// We have multiple import sources, so we need to modify the SourceInfo in the patches with a BulkImport info
		Time now = Time::getCurrentTime();
		for (auto &patch : patches) {
			auto bulkInfo = std::make_shared<FromBulkImportSource>(now, patch.sourceInfo());
			patch.setSourceInfo(bulkInfo);
		}
	}

}
