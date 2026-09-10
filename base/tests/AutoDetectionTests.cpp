#include "AutoDetection.h"
#include "Settings.h"
#include <future>
#include <iostream>
#include <thread>

namespace {
	class TestSynth : public midikraft::SimpleDiscoverableDevice {
	public:
		std::string getName() const override { return "Detection test synth"; }
		std::vector<juce::MidiMessage> deviceDetect(int) override {
			++requests;
			return {};
		}
		int deviceDetectSleepMS() override { return waitMs; }
		MidiChannel channelIfValidDeviceResponse(juce::MidiMessage const &) override { return MidiChannel::invalidChannel(); }
		bool needsChannelSpecificDetection() override { return true; }
		int requests = 0, waitMs = 10;
	};

	class CancelledProgress : public ProgressHandler {
	public:
		bool shouldAbort() const override { return true; }
		void setProgressPercentage(double) override {}
		void setMessage(std::string const &) override {}
		void onSuccess() override {}
		void onCancel() override {}
	};

	// Exercise input ownership without opening the test machine's physical MIDI ports.
	struct FakeInputs {
		// Use production ownership bookkeeping with a fake native start/stop backend.
		midikraft::MidiInputOwnership ownership;
		bool failOpen = false;
		int starts = 0, stops = 0;
		bool isMidiInputEnabled(juce::MidiDeviceInfo const &input) const { return ownership.isEnabled(input.identifier); }
		bool start() {
			if (failOpen)
				return false;
			++starts;
			return true;
		}
		bool enableMidiInput(juce::MidiDeviceInfo const &input) {
			return ownership.enable(input.identifier, [this]() { return start(); });
		}
		void disableMidiInput(juce::MidiDeviceInfo const &input) {
			ownership.disable(input.identifier, [this]() { ++stops; });
		}
		bool acquireMidiInput(juce::MidiDeviceInfo const &input) {
			return ownership.acquire(input.identifier, [this]() { return start(); });
		}
		void releaseMidiInput(juce::MidiDeviceInfo const &input) {
			ownership.release(input.identifier, [this]() { ++stops; });
		}
	};

	class DetectionTests : public juce::UnitTest {
	public:
		DetectionTests() : UnitTest("Saved connections and input restoration", "Detection") {}
		void runTest() override {
			beginTest("A saved connection remains configured while its USB ports are absent");
			auto synth = std::make_shared<TestSynth>();
			auto &settings = Settings::instance();
			expect(!midikraft::AutoDetection::hasSavedConnection(synth.get()));
			settings.set(synth->getName() + "-input", "Missing test input");
			settings.set(synth->getName() + "-output", "Missing test output");
			expect(!midikraft::AutoDetection::hasSavedConnection(synth.get()));
			for (auto channel : {"0", "15"}) {
				settings.set(synth->getName() + "-channel", channel);
				expect(midikraft::AutoDetection::hasSavedConnection(synth.get()));
			}
			for (auto channel : {"", "-1", "16", "garbage", "3oops", "999999999999999"}) {
				settings.set(synth->getName() + "-channel", channel);
				expect(!midikraft::AutoDetection::hasSavedConnection(synth.get()), channel);
			}

			beginTest("A failed saved-port check clears stale detection without scanning or erasing settings");
			settings.set(synth->getName() + "-channel", "3");
			synth->setWasDetected(true);
			midikraft::AutoDetection detector;
			std::vector<std::shared_ptr<midikraft::SimpleDiscoverableDevice>> selected{synth};
			detector.quickconfigure(selected);
			expect(!synth->wasDetected());
			expectEquals(synth->requests, 0);
			expect(midikraft::AutoDetection::hasSavedConnection(synth.get()));
			expectEquals(settings.get(synth->getName() + "-output"), std::string("Missing test output"));

			beginTest("Cancellation leaves detection and routing unchanged and sends no probes");
			CancelledProgress cancel;
			synth->setChannel(MidiChannel::fromZeroBase(7));
			detector.quickconfigure(selected, &cancel);
			detector.autoconfigure(selected, &cancel);
			expectEquals(synth->channel().toZeroBasedInt(), 7);
			expectEquals(synth->requests, 0);
			expect(!synth->wasDetected());

			beginTest("Adaptations that disable detection are not probed");
			synth->waitMs = -1;
			detector.quickconfigure(selected);
			detector.autoconfigure(selected, nullptr);
			expectEquals(synth->requests, 0);

			beginTest("A scan preserves another synth's input and releases its own temporary input");
			FakeInputs inputs;
			juce::MidiDeviceInfo existing{"Existing", "existing"}, temporary{"Temporary", "temporary"};
			inputs.enableMidiInput(existing);
			{
				midikraft::ScopedMidiInputFor<FakeInputs> keep(inputs, existing), open(inputs, temporary);
				expect(keep.isEnabled() && open.isEnabled());
				expect(inputs.isMidiInputEnabled(existing) && inputs.isMidiInputEnabled(temporary));
				{
					midikraft::ScopedMidiInputFor<FakeInputs> nested(inputs, temporary);
				}
				expect(inputs.isMidiInputEnabled(temporary));
			}
			expect(inputs.isMidiInputEnabled(existing));
			expect(!inputs.isMidiInputEnabled(temporary));
			expectEquals(inputs.stops, 1);

			beginTest("Overlapping scopes may be released in either order");
			for (bool oldestFirst : {true, false}) {
				FakeInputs overlapping;
				using Scope = midikraft::ScopedMidiInputFor<FakeInputs>;
				auto first = std::make_unique<Scope>(overlapping, temporary);
				auto second = std::make_unique<Scope>(overlapping, temporary);
				(oldestFirst ? first : second).reset();
				expect(overlapping.isMidiInputEnabled(temporary), "The remaining scope must keep the input enabled");
				expectEquals(overlapping.stops, 0);
				first.reset();
				second.reset();
				expect(!overlapping.isMidiInputEnabled(temporary));
				expectEquals(overlapping.stops, 1);
			}

			beginTest("Simultaneous acquisition starts once and neither scope can stop the other");
			{
				FakeInputs concurrent;
				std::promise<void> start, acquiredFirst, acquiredSecond, releaseFirst, releaseSecond;
				auto startSignal = start.get_future().share();
				auto firstReady = acquiredFirst.get_future(), secondReady = acquiredSecond.get_future();
				auto firstRelease = releaseFirst.get_future(), secondRelease = releaseSecond.get_future();
				bool firstEnabled = false, secondEnabled = false;
				std::thread first([&]() {
					startSignal.wait();
					midikraft::ScopedMidiInputFor<FakeInputs> scope(concurrent, temporary);
					firstEnabled = scope.isEnabled();
					acquiredFirst.set_value();
					firstRelease.wait();
				});
				std::thread second([&]() {
					startSignal.wait();
					midikraft::ScopedMidiInputFor<FakeInputs> scope(concurrent, temporary);
					secondEnabled = scope.isEnabled();
					acquiredSecond.set_value();
					secondRelease.wait();
				});
				start.set_value();
				firstReady.wait();
				secondReady.wait();
				expect(firstEnabled && secondEnabled);
				expectEquals(concurrent.starts, 1);
				releaseFirst.set_value();
				first.join();
				expect(concurrent.isMidiInputEnabled(temporary));
				expectEquals(concurrent.stops, 0);
				releaseSecond.set_value();
				second.join();
				expect(!concurrent.isMidiInputEnabled(temporary));
				expectEquals(concurrent.stops, 1);
			}

			beginTest("Explicit enables during a scan survive its final lease");
			{
				FakeInputs promoted;
				{
					midikraft::ScopedMidiInputFor<FakeInputs> scope(promoted, temporary);
					promoted.enableMidiInput(temporary);
				}
				expect(promoted.isMidiInputEnabled(temporary));
				expectEquals(promoted.stops, 0);
				promoted.disableMidiInput(temporary);
				expect(!promoted.isMidiInputEnabled(temporary));
				expectEquals(promoted.stops, 1);
			}

			beginTest("Explicit disable waits for the final temporary lease");
			{
				FakeInputs shared;
				shared.enableMidiInput(temporary);
				{
					midikraft::ScopedMidiInputFor<FakeInputs> scope(shared, temporary);
					shared.disableMidiInput(temporary);
					expect(shared.isMidiInputEnabled(temporary));
					expectEquals(shared.stops, 0);
				}
				expect(!shared.isMidiInputEnabled(temporary));
				expectEquals(shared.stops, 1);
			}

			beginTest("Disconnect and reacquire preserve a new scope when an older scope ends");
			{
				FakeInputs reconnected;
				using Scope = midikraft::ScopedMidiInputFor<FakeInputs>;
				auto old = std::make_unique<Scope>(reconnected, temporary);
				reconnected.ownership.disconnected(temporary.identifier);
				expect(!reconnected.isMidiInputEnabled(temporary));
				auto current = std::make_unique<Scope>(reconnected, temporary);
				old.reset();
				expect(reconnected.isMidiInputEnabled(temporary));
				expectEquals(reconnected.starts, 2);
				expectEquals(reconnected.stops, 0);
				current.reset();
				expectEquals(reconnected.stops, 1);
			}

			beginTest("Early unwinding restores input state");
			try {
				midikraft::ScopedMidiInputFor<FakeInputs> keep(inputs, existing), open(inputs, temporary);
				throw std::runtime_error("Detection interrupted");
			} catch (std::runtime_error const &) {
			}
			expect(inputs.isMidiInputEnabled(existing));
			expect(!inputs.isMidiInputEnabled(temporary));
			expectEquals(inputs.stops, 2);

			beginTest("A port that failed to open is not stopped during cleanup");
			inputs.failOpen = true;
			{
				midikraft::ScopedMidiInputFor<FakeInputs> failed(inputs, temporary);
				expect(!failed.isEnabled());
			}
			expectEquals(inputs.stops, 2);
			inputs.failOpen = false;
			{
				midikraft::ScopedMidiInputFor<FakeInputs> retry(inputs, temporary);
				expect(retry.isEnabled());
			}
			expectEquals(inputs.stops, 3);
		}
	} tests;
} // namespace

int main() {
	juce::ScopedJuceInitialiser_GUI juce;
	Settings::setSettingsID("KnobKraft-detection-tests-" + juce::Uuid().toString());
	auto settingsFile = Settings::instance().getPropertiesFile();
	juce::UnitTestRunner runner;
	runner.setAssertOnFailure(false);
	runner.runTestsInCategory("Detection");
	int failures = 0;
	for (int i = 0; i < runner.getNumResults(); ++i)
		failures += runner.getResult(i)->failures;
	std::cout << runner.getNumResults() << " test cases, " << failures << " failures\n";
	midikraft::MidiController::shutdown();
	Settings::shutdown();
	settingsFile.deleteFile();
	return failures == 0 ? 0 : 1;
}
