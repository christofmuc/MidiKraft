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

#include <stack>

namespace midikraft {

	class Synth;

	class Librarian {
	public:
		typedef std::function<void(std::vector<PatchHolder>)> TFinishedHandler;
		typedef std::function<void(std::vector<std::shared_ptr<DataFile>>)> TStepSequencerFinishedHandler;

		Librarian(std::vector<SynthHolder> const &synths) : synths_(synths), currentDownloadBank_(MidiBankNumber::fromZeroBase(0)), downloadNumber_(0), startDownloadNumber_(0), endDownloadNumber_(0) {}

		void startDownloadingAllPatches(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, MidiBankNumber bankNo, ProgressHandler *progressHandler, TFinishedHandler onFinished);
		void startDownloadingAllPatches(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, std::vector<MidiBankNumber> bankNo, ProgressHandler *progressHandler, TFinishedHandler onFinished);

		void downloadEditBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, TFinishedHandler onFinished);

		void startDownloadingSequencerData(std::shared_ptr<SafeMidiOutput> midiOutput, DataFileLoadCapability *sequencer, int dataFileIdentifier, ProgressHandler *progressHandler, TStepSequencerFinishedHandler onFinished);

		Synth *sniffSynth(std::vector<MidiMessage> const &messages) const;
		std::vector<PatchHolder> loadSysexPatchesFromDisk(std::shared_ptr<Synth> synth, std::shared_ptr<AutomaticCategory> automaticCategories);
		std::vector<PatchHolder> loadSysexPatchesFromDisk(std::shared_ptr<Synth> synth, std::string const &fullpath, std::string const &filename, std::shared_ptr<AutomaticCategory> automaticCategories);
		std::vector<PatchHolder> loadSysexPatchesManualDump(std::shared_ptr<Synth> synth, std::vector<MidiMessage> const &messages, std::shared_ptr<AutomaticCategory> automaticCategories);

		void saveSysexPatchesToDisk(std::vector<PatchHolder> const &patches);

		void clearHandlers();

	private:
		void startDownloadNextPatch(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth);
		void startDownloadNextDataItem(std::shared_ptr<SafeMidiOutput> midiOutput, DataFileLoadCapability *sequencer, int dataFileIdentifier);
		void handleNextStreamPart(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, const juce::MidiMessage &message, StreamLoadCapability::StreamType streamType);
		void handleNextEditBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, const juce::MidiMessage &editBuffer, MidiBankNumber bankNo);
		void handleNextBankDump(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, const juce::MidiMessage &bankDump, MidiBankNumber bankNo);

		std::vector<PatchHolder> tagPatchesWithImportFromSynth(std::shared_ptr<Synth> synth, TPatchVector &patches, MidiBankNumber bankNo);
		void tagPatchesWithMultiBulkImport(std::vector<PatchHolder> &patches);

		void updateLastPath();

		std::vector<SynthHolder> synths_;
		std::vector<MidiMessage> currentDownload_;
		MidiBankNumber currentDownloadBank_;
		std::stack<MidiController::HandlerHandle> handles_;
		TFinishedHandler onFinished_;
		TStepSequencerFinishedHandler onSequencerFinished_;
		int downloadNumber_;
		int startDownloadNumber_;
		int endDownloadNumber_;

		// To download multiple banks. This needs to go into its own context object
		TFinishedHandler nextBankHandler_;
		std::vector<midikraft::PatchHolder> currentDownloadedPatches_;
		int downloadBankNumber_;
		int endDownloadBankNumber_;

		std::string lastPath_;
	};

}
