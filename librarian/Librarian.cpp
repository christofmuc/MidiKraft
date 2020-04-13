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

#include "MidiHelpers.h"

#include <boost/format.hpp>
#include <set>
#include "Settings.h"

namespace midikraft {

	void Librarian::startDownloadingAllPatches(std::shared_ptr<SafeMidiOutput> midiOutput, Synth *synth, MidiBankNumber bankNo,
		ProgressHandler *progressHandler, TFinishedHandler onFinished)
	{
		// Ok, for this we need to send a program change message, and then a request edit buffer message from the active synth
		// Once we get that, store the patch and increment number by one
		downloadNumber_ = 0;
		currentDownload_.clear();
		onFinished_ = onFinished;

		// Determine what we will do with the answer...
		handle_ = MidiController::makeOneHandle();
		auto streamLoading = dynamic_cast<StreamLoadCapability*>(synth);
		auto bankCapableSynth = dynamic_cast<BankDumpCapability *>(synth);
		auto handshakeLoadingRequired = dynamic_cast<HandshakeLoadingCapability *>(synth);
		if (streamLoading) {
			// Simple enough, we hope
			MidiController::instance()->addMessageHandler(handle_, [this, synth, progressHandler, midiOutput, bankNo](MidiInput *source, const juce::MidiMessage &editBuffer) {
				ignoreUnused(source);
				this->handleNextStreamPart(midiOutput, synth, progressHandler, editBuffer, bankNo);
			});
			currentDownload_.clear();
			startDownloadNextPatch(midiOutput, synth);
		}
		else if (handshakeLoadingRequired) {
			// These are proper protocols that are implemented - each message we get from the synth has to be answered by an appropriate next message
			std::shared_ptr<HandshakeLoadingCapability::ProtocolState>  state = handshakeLoadingRequired->createStateObject();
			if (state) {
				MidiController::instance()->addMessageHandler(handle_, [this, handshakeLoadingRequired, state, progressHandler, midiOutput, synth, bankNo](MidiInput *source, const juce::MidiMessage &protocolMessage) {
					ignoreUnused(source);
					std::vector<MidiMessage> answer;
					if (handshakeLoadingRequired->isNextMessage(protocolMessage, answer, state)) {
						// Store message
						currentDownload_.push_back(protocolMessage);
					}
					// Send an answer if the handshake handler constructed one
					if (!answer.empty()) {
						auto buffer = MidiHelpers::bufferFromMessages(answer);
						midiOutput->sendBlockOfMessagesNow(buffer);
					}
					// Update progress handler
					progressHandler->setProgressPercentage(state->progress());

					// Stop handler when finished
					if (state->isFinished() || progressHandler->shouldAbort()) {
						MidiController::instance()->removeMessageHandler(handle_);
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
				handshakeLoadingRequired->startDownload(midiOutput, state);
			}
			else {
				jassert(false);
			}
		}
		else if (bankCapableSynth) {
			// This is a mixture - you send one message (bank request), and then you get either one message back (like Kawai K3) or a stream of messages with
			// one message per patch (e.g. Access Virus or Matrix1000)
			MidiController::instance()->addMessageHandler(handle_, [this, synth, progressHandler, midiOutput, bankNo](MidiInput *source, const juce::MidiMessage &editBuffer) {
				ignoreUnused(source);
				this->handleNextBankDump(midiOutput, synth, progressHandler, editBuffer, bankNo);
			});
			currentDownload_.clear();
			midiOutput->sendBlockOfMessagesNow(MidiHelpers::bufferFromMessages(bankCapableSynth->requestBankDump(bankNo)));
		}
		else {
			// Uh, stone age, need to start a loop
			MidiController::instance()->addMessageHandler(handle_, [this, synth, progressHandler, midiOutput, bankNo](MidiInput *source, const juce::MidiMessage &editBuffer) {
				ignoreUnused(source);
				this->handleNextEditBuffer(midiOutput, synth, progressHandler, editBuffer, bankNo);
			});
			downloadNumber_ = bankNo.toZeroBased() * synth->numberOfPatches();
			startDownloadNumber_ = downloadNumber_;
			endDownloadNumber_ = downloadNumber_ + synth->numberOfPatches() - 1;
			startDownloadNextPatch(midiOutput, synth);
		}
	}

	void Librarian::downloadEditBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, Synth *synth, ProgressHandler *progressHandler, TFinishedHandler onFinished)
	{
		// Ok, for this we need to send a program change message, and then a request edit buffer message from the active synth
		// Once we get that, store the patch and increment number by one
		downloadNumber_ = 0;
		currentDownload_.clear();
		onFinished_ = onFinished;
		// Uh, stone age, need to start a loop
		handle_ = MidiController::makeOneHandle();
		MidiController::instance()->addMessageHandler(handle_, [this, synth, progressHandler, midiOutput](MidiInput *source, const juce::MidiMessage &editBuffer) {
			ignoreUnused(source);
			this->handleNextEditBuffer(midiOutput, synth, progressHandler, editBuffer, MidiBankNumber::fromZeroBase(0));
		});
		// Special case - load only a single patch. In this case we're interested in the edit buffer only!
		startDownloadNumber_ = 0;
		endDownloadNumber_ = 0;
		auto editBufferCapability = dynamic_cast<EditBufferCapability *>(synth);
		if (editBufferCapability) {
			auto message = editBufferCapability->requestEditBufferDump();
			midiOutput->sendMessageNow(message);
		}
	}

	void Librarian::startDownloadingSequencerData(std::shared_ptr<SafeMidiOutput> midiOutput, DataFileLoadCapability *sequencer, int dataFileIdentifier, ProgressHandler *progressHandler, TStepSequencerFinishedHandler onFinished)
	{
		downloadNumber_ = 0;
		currentDownload_.clear();
		onSequencerFinished_ = onFinished;

		handle_ = MidiController::makeOneHandle();
		MidiController::instance()->addMessageHandler(handle_, [this, sequencer, progressHandler, midiOutput, dataFileIdentifier](MidiInput *source, const MidiMessage &message) {
			ignoreUnused(source);
			if (sequencer->isDataFile(message, dataFileIdentifier)) {
				currentDownload_.push_back(message);
				downloadNumber_++;
				if (downloadNumber_ >= sequencer->numberOfDataItemsPerType(dataFileIdentifier)) {
					auto loadedData = sequencer->loadData(currentDownload_, dataFileIdentifier);
					MidiController::instance()->removeMessageHandler(handle_);
					onSequencerFinished_(loadedData);
					if (progressHandler) progressHandler->onSuccess();
				}
				else if (progressHandler->shouldAbort()) {
					MidiController::instance()->removeMessageHandler(handle_);
					if (progressHandler) progressHandler->onCancel();
				}
				else {
					startDownloadNextDataItem(midiOutput, sequencer, dataFileIdentifier);
					if (progressHandler) progressHandler->setProgressPercentage(downloadNumber_ / (double)sequencer->numberOfDataItemsPerType(dataFileIdentifier));
				}
			}
		});
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
		LoadManyPatchFiles(Librarian *librarian, Synth &synth, Array<File> files) : ThreadWithProgressWindow("Loading patch files", true, true), librarian_(librarian), synth_(synth), files_(files)
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
				auto newPatches = librarian_->loadSysexPatchesFromDisk(synth_, pathChosen, fileChosen.getFileName().toStdString());
				std::copy(newPatches.begin(), newPatches.end(), std::back_inserter(result_));
				filesDone++;
			}
		}

		std::vector<PatchHolder> result() {
			return result_;
		}

	private:
		Librarian *librarian_;
		Synth &synth_;
		Array<File> files_;
		std::vector<PatchHolder> result_;
	};

	std::vector<PatchHolder> Librarian::loadSysexPatchesFromDisk(Synth &synth)
	{
		if (lastPath_.empty()) {
			// Read from settings
			lastPath_ = Settings::instance().get("lastImportPath", "");
			if (lastPath_.empty()) {
				// Default directory
				lastPath_ = File::getSpecialLocation(File::userDocumentsDirectory).getFullPathName().toStdString();
			}
		}
		FileChooser sysexChooser("Please select the sysex you want to load...",
			File(lastPath_),
			"*.syx;*.mid;*.zip;*.txt");
		if (sysexChooser.browseForMultipleFilesToOpen())
		{
			if (sysexChooser.getResults().size() > 0) {
				// The first file is used to store the path for the next operation
				lastPath_ = sysexChooser.getResults()[0].getParentDirectory().getFullPathName().toStdString();
				Settings::instance().set("lastImportPath", lastPath_);
			}

			LoadManyPatchFiles backgroundTask(this, synth, sysexChooser.getResults());
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

	std::vector<PatchHolder> Librarian::loadSysexPatchesFromDisk(Synth &synth, std::string const &fullpath, std::string const &filename) {
		auto messagesLoaded = Sysex::loadSysex(fullpath);
		auto patches = synth.loadSysex(messagesLoaded);

		if (patches.empty()) {
			// Bugger - probably the file is for some synth that is correctly not the active one... happens frequently for me
			// Let's try to sniff the synth from the magics given and then try to reload the file with the correct synth
			auto detectedSynth = sniffSynth(messagesLoaded);
			if (detectedSynth) {
				// That's better, now try again
				//patches = detectedSynth->loadSysex(messagesLoaded);
			}
		}

		// Add the meta information
		std::vector<PatchHolder> result;
		int i = 0;
		for (auto patch : patches) {
			result.push_back(PatchHolder(&synth, std::make_shared<FromFileSource>(filename, fullpath, MidiProgramNumber::fromZeroBase(i)), patch, true));
			i++;
		}
		return result;
	}

	void Librarian::startDownloadNextPatch(std::shared_ptr<SafeMidiOutput> midiOutput, Synth *synth) {
		auto editBufferCapability = dynamic_cast<EditBufferCapability *>(synth);
		auto programDumpCapability = dynamic_cast<ProgramDumpCabability *>(synth);

		// Get all commands
		std::vector<MidiMessage> messages;
		if (programDumpCapability) {
			messages = programDumpCapability->requestPatch(downloadNumber_);
		}
		else if (editBufferCapability) {
			//messages.push_back(MidiMessage::controllerEvent(synth->channel().toOneBasedInt(), 32, bankNo));
			messages.push_back(MidiMessage::programChange(synth->channel().toOneBasedInt(), downloadNumber_));
			messages.push_back(editBufferCapability->requestEditBufferDump());
		}
		else {
			jassert(false);
		}

		// Send messages
		auto buffer = MidiHelpers::bufferFromMessages(messages);
		midiOutput->sendBlockOfMessagesNow(buffer);
	}

	void Librarian::startDownloadNextDataItem(std::shared_ptr<SafeMidiOutput> midiOutput, DataFileLoadCapability *sequencer, int dataFileIdentifier) {
		std::vector<MidiMessage> request = sequencer->requestDataItem(downloadNumber_, dataFileIdentifier);
		auto buffer = MidiHelpers::bufferFromMessages(request);
		midiOutput->sendBlockOfMessagesNow(buffer);
	}

	void Librarian::handleNextStreamPart(std::shared_ptr<SafeMidiOutput> midiOutput, Synth *synth, ProgressHandler *progressHandler, const juce::MidiMessage &editBuffer, MidiBankNumber bankNo)
	{
		auto streamLoading = dynamic_cast<StreamLoadCapability*>(synth);
		if (streamLoading) {
			if (streamLoading->isMessagePartOfStream(editBuffer)) {
				currentDownload_.push_back(editBuffer);
				if (streamLoading->isStreamComplete(currentDownload_)) {
					MidiController::instance()->removeMessageHandler(handle_);
					auto result = synth->loadSysex(currentDownload_);
					onFinished_(tagPatchesWithImportFromSynth(synth, result, bankNo));
					progressHandler->onSuccess();
				}
				else if (progressHandler->shouldAbort()) {
					MidiController::instance()->removeMessageHandler(handle_);
					progressHandler->onCancel();
				}
				else if (streamLoading->shouldStreamAdvance(currentDownload_)) {
					downloadNumber_++;
					startDownloadNextPatch(midiOutput, synth);
					progressHandler->setProgressPercentage(downloadNumber_ / (double)synth->numberOfPatches());
				}
			}
		}
		else {
			jassert(false);
		}
	}

	void Librarian::handleNextEditBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, Synth *synth, ProgressHandler *progressHandler, const juce::MidiMessage &editBuffer, MidiBankNumber bankNo) {
		auto editBufferCapability = dynamic_cast<EditBufferCapability *>(synth);
		auto programDumpCapability = dynamic_cast<ProgramDumpCabability *>(synth);
		if ((editBufferCapability  && editBufferCapability->isEditBufferDump(editBuffer)) ||
			(programDumpCapability && programDumpCapability->isSingleProgramDump(editBuffer))) {
			// Ok, that worked, save it and continue!
			currentDownload_.push_back(MidiMessage(editBuffer));
			if (editBufferCapability  && editBufferCapability->isEditBufferDump(editBuffer)) {
				// For reloading, also persist a message in the file that will force the Synth to store the edit buffer so you can reload the file with any sysex tool
				//TODO Not all synths support this, so this should become a capability
				currentDownload_.push_back(editBufferCapability->saveEditBufferToProgram(downloadNumber_));
			}

			// Finished?
			if (downloadNumber_ >= endDownloadNumber_) {
				MidiController::instance()->removeMessageHandler(handle_);
				auto patches = synth->loadSysex(currentDownload_);
				onFinished_(tagPatchesWithImportFromSynth(synth, patches, bankNo));
				if (progressHandler) progressHandler->onSuccess();
			}
			else if (progressHandler->shouldAbort()) {
				MidiController::instance()->removeMessageHandler(handle_);
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

	void Librarian::handleNextBankDump(std::shared_ptr<SafeMidiOutput> midiOutput, Synth *synth, ProgressHandler *progressHandler, const juce::MidiMessage &bankDump, MidiBankNumber bankNo)
	{
		ignoreUnused(midiOutput); //TODO why?
		auto bankDumpCapability = dynamic_cast<BankDumpCapability*>(synth);
		if (bankDumpCapability && bankDumpCapability->isBankDump(bankDump)) {
			currentDownload_.push_back(bankDump);
			if (bankDumpCapability->isBankDumpFinished(currentDownload_)) {
				MidiController::instance()->removeMessageHandler(handle_);
				auto patches = synth->loadSysex(currentDownload_);
				onFinished_(tagPatchesWithImportFromSynth(synth, patches, bankNo));
				progressHandler->onSuccess();
			}
			else if (progressHandler->shouldAbort()) {
				MidiController::instance()->removeMessageHandler(handle_);
				progressHandler->onCancel();
			}
			else {
				progressHandler->setProgressPercentage(currentDownload_.size() / (double)synth->numberOfPatches());
			}
		}
	}

	std::vector<PatchHolder> Librarian::tagPatchesWithImportFromSynth(Synth *synth, TPatchVector &patches, MidiBankNumber bankNo) {
		std::vector<PatchHolder> result;
		auto now = Time::getCurrentTime();
		for (auto patch : patches) {
			result.push_back(PatchHolder(synth, std::make_shared<FromSynthSource>(now, bankNo), patch, true));
		}
		return result;
	}

}
