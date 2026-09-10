#include "AutoDetection.h"
#include "Settings.h"

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
    std::set<juce::String> enabled;
    bool failOpen = false;
    int stops = 0;
    bool isMidiInputEnabled(juce::MidiDeviceInfo const &input) const { return enabled.count(input.identifier) != 0; }
    bool enableMidiInput(juce::MidiDeviceInfo const &input) {
	if (failOpen)
	    return false;
	enabled.insert(input.identifier);
	return true;
    }
    void disableMidiInput(juce::MidiDeviceInfo const &input) {
	enabled.erase(input.identifier);
	++stops;
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
	inputs.enabled.insert(existing.identifier);
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
    midikraft::MidiController::shutdown();
    Settings::shutdown();
    settingsFile.deleteFile();
    return failures == 0 ? 0 : 1;
}
