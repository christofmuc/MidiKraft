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

#include <vector>

namespace midikraft {

	class Synth;

	class Librarian {
	public:
		typedef std::function<void(std::vector<PatchHolder>)> TFinishedHandler;
		typedef std::function<void(std::vector<std::shared_ptr<DataFile>>)> TStepSequencerFinishedHandler;

		Librarian(std::vector<SynthHolder> const &synths) : synths_(synths), downloadNumber_(0), startDownloadNumber_(0), endDownloadNumber_(0) {}

		void startDownloadingAllPatches(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, MidiBankNumber bankNo,
			ProgressHandler *progressHandler, TFinishedHandler onFinished);

		void downloadEditBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, TFinishedHandler onFinished);

		void startDownloadingSequencerData(std::shared_ptr<SafeMidiOutput> midiOutput, DataFileLoadCapability *sequencer, int dataFileIdentifier, ProgressHandler *progressHandler, TStepSequencerFinishedHandler onFinished);

		Synth *sniffSynth(std::vector<MidiMessage> const &messages) const;
		std::vector<PatchHolder> loadSysexPatchesFromDisk(std::shared_ptr<Synth> synth);
		std::vector<PatchHolder> loadSysexPatchesFromDisk(std::shared_ptr<Synth> synth, std::string const &fullpath, std::string const &filename);

	private:
		void startDownloadNextPatch(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth);
		void startDownloadNextDataItem(std::shared_ptr<SafeMidiOutput> midiOutput, DataFileLoadCapability *sequencer, int dataFileIdentifier);
		void handleNextStreamPart(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, const juce::MidiMessage &editBuffer, MidiBankNumber bankNo);
		void handleNextEditBuffer(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, const juce::MidiMessage &editBuffer, MidiBankNumber bankNo);
		void handleNextBankDump(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, ProgressHandler *progressHandler, const juce::MidiMessage &bankDump, MidiBankNumber bankNo);
		std::vector<PatchHolder> tagPatchesWithImportFromSynth(std::shared_ptr<Synth> synth, TPatchVector &patches, MidiBankNumber bankNo);

		std::vector<SynthHolder> synths_;
		std::vector<MidiMessage> currentDownload_;
		MidiController::HandlerHandle handle_ = MidiController::makeNoneHandle();
		TFinishedHandler onFinished_;
		TStepSequencerFinishedHandler onSequencerFinished_;
		int downloadNumber_;
		int startDownloadNumber_;
		int endDownloadNumber_;
		std::string lastPath_;
	};

}
