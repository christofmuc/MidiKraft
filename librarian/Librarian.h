/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "MidiController.h"
#include "Patch.h"
#include "ProgressHandler.h"
#include "MidiBankNumber.h"
#include "SynthHolder.h"
#include "PatchHolder.h"
#include "DataFileLoadCapability.h"
#include "StreamLoadCapability.h"
#include "SynthBank.h"

#include <stack>

namespace midikraft {

	class Synth;

	class Librarian {
	public:
		typedef std::function<void(std::vector<PatchHolder>)> TFinishedHandler;
		typedef std::function<void(std::vector<std::shared_ptr<DataFile>>)> TStepSequencerFinishedHandler;

		Librarian(std::vector<SynthHolder> const &synths) : synths_(synths), currentDownloadBank_(MidiBankNumber::invalid()), downloadNumber_(0), startDownloadNumber_(0), endDownloadNumber_(0) {}

		void startDownloadingAllPatches(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, MidiBankNumber bankNo, ProgressHandler *progressHandler, TFinishedHandler onFinished);
		void startDownloadingAllPatches(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, std::vector<MidiBankNumber> bankNo, ProgressHandler *progressHandler, TFinishedHandler onFinished);

		void downloadEditBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, TFinishedHandler onFinished);

		void startDownloadingSequencerData(std::shared_ptr<SafeMidiOutput> midiOutput, DataFileLoadCapability *sequencer, int dataFileIdentifier, ProgressHandler *progressHandler, TStepSequencerFinishedHandler onFinished);

		Synth *sniffSynth(std::vector<MidiMessage> const &messages) const;
		std::vector<PatchHolder> loadSysexPatchesFromDisk(std::shared_ptr<Synth> synth, std::shared_ptr<AutomaticCategory> automaticCategories);
		std::vector<PatchHolder> loadSysexPatchesFromDisk(std::shared_ptr<Synth> synth, std::string const &fullpath, std::string const &filename, std::shared_ptr<AutomaticCategory> automaticCategories);
		std::vector<PatchHolder> loadSysexPatchesManualDump(std::shared_ptr<Synth> synth, std::vector<MidiMessage> const &messages, std::shared_ptr<AutomaticCategory> automaticCategories);

		void sendBankToSynth(SynthBank const& synthBank, bool fullBank, ProgressHandler *progressHandler, std::function<void(bool completed)> finishedHandler);

		enum ExportFormatOption {
			PROGRAM_DUMPS = 0,
			EDIT_BUFFER_DUMPS = 1
		};
		enum ExportFileOption {
			MANY_FILES = 0,
			ZIPPED_FILES = 1,
			ONE_FILE = 2,
			MID_FILE = 3
		};

		struct ExportParameters {
			int formatOption;
			int fileOption;
		};
		void saveSysexPatchesToDisk(ExportParameters params, std::vector<PatchHolder> const &patches);

		void clearHandlers();

	private:
		void startDownloadNextEditBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, bool sendProgramChange);
		void startDownloadNextPatch(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth);
		void startDownloadNextDataItem(std::shared_ptr<SafeMidiOutput> midiOutput, DataFileLoadCapability *sequencer, int dataFileIdentifier);
		void handleNextStreamPart(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, const juce::MidiMessage &message, StreamLoadCapability::StreamType streamType);
		void handleNextEditBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, const juce::MidiMessage &editBuffer, MidiBankNumber bankNo);
		void handleNextProgramBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler* progressHandler, const juce::MidiMessage& editBuffer, MidiBankNumber bankNo);
		void handleNextBankDump(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler* progressHandler, const juce::MidiMessage& bankDump, MidiBankNumber bankNo);

		std::vector<PatchHolder> tagPatchesWithImportFromSynth(std::shared_ptr<Synth> synth, TPatchVector &patches, MidiBankNumber bankNo);
		void tagPatchesWithMultiBulkImport(std::vector<PatchHolder> &patches);

		void updateLastPath(std::string &lastPathVariable, std::string const &settingsKey);

		std::vector<SynthHolder> synths_;
		std::vector<MidiMessage> currentDownload_;
		std::vector<MidiMessage> currentEditBuffer_;
		std::vector<MidiMessage> currentProgramDump_;
		MidiBankNumber currentDownloadBank_;
		std::stack<MidiController::HandlerHandle> handles_;
		TFinishedHandler onFinished_;
		TStepSequencerFinishedHandler onSequencerFinished_;
		int downloadNumber_;
		int startDownloadNumber_;
		int endDownloadNumber_;
		int expectedDownloadNumber_;

		// To download multiple banks. This needs to go into its own context object
		TFinishedHandler nextBankHandler_;
		std::vector<midikraft::PatchHolder> currentDownloadedPatches_;
		int downloadBankNumber_;
		//int endDownloadBankNumber_;

		std::string lastPath_; // Last import path
		std::string lastExportDirectory_; 
		std::string lastExportZipFilename_;
		std::string lastExportSyxFilename_;
		std::string lastExportMidFilename_;
	};

}
