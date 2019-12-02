/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "SimpleDiscoverableDevice.h"
#include "MidiProgramNumber.h"
#include "MidiBankNumber.h"

namespace midikraft {

	class Patch;
	class MidiController;
	class EditBufferHandler;
	class SimpleLogger;
	class BCR2000;

	typedef std::vector<std::shared_ptr<Patch>> TPatchVector;

	class Synth : public SimpleDiscoverableDevice {
	public:
		typedef std::vector<uint8> PatchData;

		virtual ~Synth() = default;

		virtual int numberOfBanks() const = 0;
		virtual int numberOfPatches() const = 0;
		virtual std::string friendlyBankName(MidiBankNumber bankNo) const = 0;

		virtual std::shared_ptr<Patch> patchFromPatchData(const Synth::PatchData &data, std::string const &name, MidiProgramNumber place) const = 0;
		virtual bool isOwnSysex(MidiMessage const &message) const = 0;

		// Override this in case not all bytes contribute to the sound of the patch
		virtual PatchData filterVoiceRelevantData(PatchData const &unfilteredData) const;

		// Generic functions that should function with most Synthesizers, only overload if required
		virtual TPatchVector loadSysex(std::vector<MidiMessage> const &sysexMessages);
		virtual void saveSysex(std::string const &filename, std::vector<MidiMessage> messages);
		virtual void sendPatchToSynth(MidiController *controller, EditBufferHandler *handler, SimpleLogger *logger, Patch const &patch);
	};

}
