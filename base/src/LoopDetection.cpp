/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "LoopDetection.h"

#include "MidiHelpers.h"

namespace midikraft {

	class RunMidiLoopDetection : public Thread {
	public:
		RunMidiLoopDetection(std::weak_ptr<ProgressHandler> progressHandler) : Thread("MIDI Loop Detection"), progressHandler_(progressHandler) {
			MidiController::instance()->addMessageHandler(handler_, [this](MidiInput *source, MidiMessage const &midimessage) {
				handleIncomingMidiMessage(source, midimessage);
			});
		}

		virtual ~RunMidiLoopDetection() override
		{
			MidiController::instance()->removeMessageHandler(handler_);
		}

		void run() override
		{
			// Listen to all devices connected at the same time
			for (auto const &input : MidiInput::getAvailableDevices()) {
				MidiController::instance()->enableMidiInput(input);
			}

			// Now loop over outputs
			size_t output_count = 0;
			for (auto const &output: MidiOutput::getAvailableDevices()) {
				if (!progressHandler_.expired() && progressHandler_.lock()->shouldAbort()) break;

				// Just one message is enough - test sysex loop
				currentOut_ = output;
				MidiController::instance()->getMidiOutput(currentOut_)->sendMessageNow(loopDetectionSysexMessage_);

				// Then test note loop
				MidiController::instance()->getMidiOutput(currentOut_)->sendMessageNow(loopDetectionNoteOnMessage_);

				// Sleep
				Thread::sleep(200);

				// must check this as often as possible, because this is
				// how we know if the user's pressed 'cancel'
				if (threadShouldExit())
					break;

				// this will update the progress bar on the dialog box
				if (!progressHandler_.expired()) {
					progressHandler_.lock()->setProgressPercentage(++output_count / (double)MidiOutput::getAvailableDevices().size());
				}
			}
		}

	private:
		friend class LoopDetection;

		MidiController::HandlerHandle handler_;
		std::weak_ptr<ProgressHandler> progressHandler_;

		// Use a MIDI Sysex Universal Device Detect message and see if this comes back
		//MidiMessage loopDetectionSysexMessage_ = MidiHelpers::sysexMessage({ 0x7e, 0x7f /* all sysex channels */, 0x06, 0x01 });
		MidiMessage loopDetectionSysexMessage_ = MidiHelpers::sysexMessage({ 0x7e, 0x7f /* all sysex channels */, 0x06, 0x02 }); // Useless "Identity Reply" package
		MidiMessage loopDetectionNoteOnMessage_ = MidiMessage::noteOn(0xf, 0, (uint8) 1); // Don't use velocity 0, as this might be called erroneously note-off by JUCE

		juce::MidiDeviceInfo currentOut_;
		std::vector<MidiLoop> loops_;

		void handleIncomingMidiMessage(MidiInput * source, MidiMessage const & midimessage)
		{
			 // See if this is our sysex loop detection message
			if (midimessage.isSysEx()) {
				if (MidiHelpers::equalSysexMessageContent(midimessage, loopDetectionSysexMessage_)) {
					// Looks like a loop
					loops_.push_back({ currentOut_, source->getDeviceInfo(), MidiLoopType::Sysex });
				}
			}
			else if (midimessage.isNoteOn()) {
				if (midimessage.getChannel() == 0x0f && midimessage.getNoteNumber() == 0 && midimessage.getVelocity() == 1) {
					// Oops, this is a loop
					loops_.push_back({ currentOut_, source->getDeviceInfo(), MidiLoopType::Note });
				}
			}
		}
	};

	std::vector<MidiLoop> LoopDetection::detectLoops(std::weak_ptr<ProgressHandler> progressHandler)
	{
		RunMidiLoopDetection detector(progressHandler);
		detector.startThread();
		if (detector.waitForThreadToExit(15000))
		{
			// thread finished normally..
			return detector.loops_;
		}
		else
		{
			// user pressed the cancel button respectively timeout, return empty list
			return std::vector<MidiLoop>();
		}

	}

}
