/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "MidiController.h"

namespace midikraft {

	class HandshakeLoadingCapability {
	public:
		struct ProtocolState {
			virtual bool isFinished() = 0;
			virtual bool wasSuccessful() = 0;
			virtual double progress() = 0;
		};
		virtual std::shared_ptr<ProtocolState> createStateObject() = 0;
		virtual void startDownload(SafeMidiOutput *output, std::shared_ptr<ProtocolState> saveState) = 0;
		virtual bool isNextMessage(MidiMessage const &message, std::vector<MidiMessage> &answer, std::shared_ptr<ProtocolState> state) = 0;
	};

}