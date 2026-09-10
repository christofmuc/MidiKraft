#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "MidiController.h"

namespace midikraft {

	struct MidiControllerTestAccess {
		static void setSecondarySender(MidiController& controller, MidiDeviceInfo const& device,
			std::function<void(const MidiMessage&)> sender) {
			controller.secondaryMidiOutputIdentifier_ = device.identifier;
			controller.secondaryMidiSender_ = std::move(sender);
		}

		static void sent(MidiController& controller, MidiMessage const& message, MidiDeviceInfo const& device) {
			controller.midiMessageSent(message, device);
		}

		static void stopTimer(MidiController& controller) {
			controller.stopTimer();
		}

		static void setLastActivity(MidiController& controller, MidiController::HandlerHandle const& handle, uint32 timestamp) {
			controller.messageHandlers_.at(handle).lastActivityMs = timestamp;
		}

		static uint32 lastActivity(MidiController& controller, MidiController::HandlerHandle const& handle) {
			return controller.messageHandlers_.at(handle).lastActivityMs;
		}

		static bool hasExpiredHandler(MidiController& controller, uint32 now) {
			return !controller.collectExpiredHandlers(now).empty();
		}

		static void deliverPartialSysex(MidiController& controller) {
			const uint8 partialData[] = { 0xf0, 0x42 };
			controller.handlePartialSysexMessage(nullptr, partialData, 2, 0.0);
		}

		static bool timeoutSurvivesPartialActivity(MidiController& controller, uint32 now) {
			auto pending = controller.collectExpiredHandlers(now);
			REQUIRE(pending.size() == 1);
			deliverPartialSysex(controller);
			return static_cast<bool>(controller.beginTimeoutDispatch(pending.front()));
		}

		static MidiCallback beginTimeoutDispatch(MidiController& controller, uint32 now) {
			auto pending = controller.collectExpiredHandlers(now);
			REQUIRE(pending.size() == 1);
			return controller.beginTimeoutDispatch(pending.front());
		}
	};

}

namespace {
	std::vector<uint8> bytesOf(MidiMessage const& message) {
		return { message.getRawData(), message.getRawData() + message.getRawDataSize() };
	}

	class MidiControllerFixture {
	public:
		MidiControllerFixture() : controller_(new midikraft::MidiController()) {
			midikraft::MidiControllerTestAccess::stopTimer(*controller_);
		}
		~MidiControllerFixture() {
			midikraft::MidiController::shutdown();
		}
		midikraft::MidiController& controller() const { return *controller_; }
	private:
		juce::ScopedJuceInitialiser_GUI juce_;
		midikraft::MidiController* controller_;
	};

}

TEST_CASE("timeout sentinel is safely constructed and distinct from an empty SysEx", "[MidiController]") {
	auto timeout = midikraft::MidiController::makeTimeoutMessage();
	CHECK(timeout.getRawDataSize() == 0);
	CHECK(midikraft::MidiController::isTimeoutMessage(timeout));
	CHECK_FALSE(midikraft::MidiController::isTimeoutMessage(MidiMessage()));
}

TEST_CASE("secondary MIDI receives every outgoing message regardless of log filtering", "[MidiController][secondary]") {
	MidiControllerFixture fixture;
	auto& controller = fixture.controller();
	std::vector<MidiMessage> mirrored, logged;
	const MidiDeviceInfo primary{ "Synth", "primary" }, secondary{ "Editor", "secondary" };
	midikraft::MidiControllerTestAccess::setSecondarySender(controller, secondary,
		[&](const MidiMessage& message) { mirrored.push_back(message); });
	controller.setMidiLogFunction([&](const MidiMessage& message, const String&, bool) { logged.push_back(message); });
	const uint8 sysex[] = { 0x7d, 0x01 };
	const std::vector<MidiMessage> messages{
		MidiMessage::programChange(3, 42), MidiMessage::controllerEvent(3, 0, 2),
		MidiMessage::noteOn(3, 60, static_cast<uint8>(100)), MidiMessage::noteOff(3, 60),
		MidiMessage::createSysExMessage(sysex, 2), MidiMessage::midiClock(), MidiMessage(0xfe)
	};
	for (auto level : { midikraft::MidiLogLevel::SYSEX_ONLY, midikraft::MidiLogLevel::ALL_BUT_REALTIME }) {
		controller.setMidiLogLevel(level);
		mirrored.clear();
		logged.clear();
		for (const auto& message : messages) midikraft::MidiControllerTestAccess::sent(controller, message, primary);
		REQUIRE(mirrored.size() == messages.size());
		for (size_t i = 0; i < messages.size(); ++i) {
			CHECK(bytesOf(mirrored[i]) == bytesOf(messages[i]));
		}
		CHECK(logged.size() == (level == midikraft::MidiLogLevel::SYSEX_ONLY ? 1 : 5));
	}
	// Installing a logger is not required for routing either.
	controller.setMidiLogFunction({});
	mirrored.clear();
	midikraft::MidiControllerTestAccess::sent(controller, messages.front(), primary);
	CHECK(mirrored.size() == 1);
}

TEST_CASE("secondary MIDI distinguishes device IDs and excludes incoming traffic", "[MidiController][secondary]") {
	MidiControllerFixture fixture;
	auto& controller = fixture.controller();
	int count = 0;
	const MidiDeviceInfo primary{ "Same display name", "primary" }, secondary{ "Same display name", "secondary" };
	midikraft::MidiControllerTestAccess::setSecondarySender(controller, secondary,
		[&](const MidiMessage&) { ++count; });
	auto message = MidiMessage::programChange(1, 12);
	midikraft::MidiControllerTestAccess::sent(controller, message, primary);
	CHECK(count == 1);
	midikraft::MidiControllerTestAccess::sent(controller, message, secondary);
	CHECK(count == 1);
	controller.logMidiMessage(message, "Keyboard", false);
	CHECK(count == 1);
	controller.setSecondaryMidiOutput({});
	midikraft::MidiControllerTestAccess::sent(controller, message, primary);
	CHECK(count == 1);
	CHECK_FALSE(controller.sendToSecondaryMidiOut({ message }));
}

TEST_CASE("explicit secondary output preserves order and ignores empty messages", "[MidiController][secondary]") {
	MidiControllerFixture fixture;
	auto& controller = fixture.controller();
	std::vector<MidiMessage> sent;
	midikraft::MidiControllerTestAccess::setSecondarySender(controller, { "Editor", "secondary" },
		[&](const MidiMessage& message) { sent.push_back(message); });
	auto first = MidiMessage::controllerEvent(1, 0, 2);
	auto second = MidiMessage::programChange(1, 42);
	REQUIRE(controller.sendToSecondaryMidiOut({ first, midikraft::MidiController::makeTimeoutMessage(), MidiMessage(), second }));
	REQUIRE(sent.size() == 2);
	CHECK(bytesOf(sent[0]) == bytesOf(first));
	CHECK(bytesOf(sent[1]) == bytesOf(second));
}

TEST_CASE("partial SysEx activity refreshes an opted-in inactivity timeout", "[MidiController]") {
	MidiControllerFixture fixture;
	auto& controller = fixture.controller();
	auto handle = midikraft::MidiController::makeOneHandle();
	controller.addMessageHandler(handle, [](juce::MidiInput*, juce::MidiMessage const&) {}, 100,
		midikraft::MidiController::TimeoutActivity::INCLUDE_PARTIAL_SYSEX);

	midikraft::MidiControllerTestAccess::setLastActivity(controller, handle, 100);
	midikraft::MidiControllerTestAccess::deliverPartialSysex(controller);
	auto refreshedAt = midikraft::MidiControllerTestAccess::lastActivity(controller, handle);

	CHECK_FALSE(midikraft::MidiControllerTestAccess::hasExpiredHandler(controller, refreshedAt + 99));
	CHECK(midikraft::MidiControllerTestAccess::hasExpiredHandler(controller, refreshedAt + 100));
}

TEST_CASE("partial SysEx activity does not refresh unrelated handlers", "[MidiController]") {
	MidiControllerFixture fixture;
	auto& controller = fixture.controller();
	auto handle = midikraft::MidiController::makeOneHandle();
	controller.addMessageHandler(handle, [](juce::MidiInput*, juce::MidiMessage const&) {}, 100);
	midikraft::MidiControllerTestAccess::setLastActivity(controller, handle, 100);

	midikraft::MidiControllerTestAccess::deliverPartialSysex(controller);

	CHECK(midikraft::MidiControllerTestAccess::lastActivity(controller, handle) == 100);
	CHECK(midikraft::MidiControllerTestAccess::hasExpiredHandler(controller, 200));
}

TEST_CASE("partial SysEx activity invalidates a timeout awaiting dispatch", "[MidiController]") {
	MidiControllerFixture fixture;
	auto& controller = fixture.controller();
	auto handle = midikraft::MidiController::makeOneHandle();
	controller.addMessageHandler(handle, [](juce::MidiInput*, juce::MidiMessage const&) {}, 100,
		midikraft::MidiController::TimeoutActivity::INCLUDE_PARTIAL_SYSEX);
	midikraft::MidiControllerTestAccess::setLastActivity(controller, handle, 100);

	CHECK_FALSE(midikraft::MidiControllerTestAccess::timeoutSurvivesPartialActivity(controller, 200));
}

TEST_CASE("partial SysEx activity does not revoke a timeout that entered dispatch", "[MidiController]") {
	MidiControllerFixture fixture;
	auto& controller = fixture.controller();
	auto handle = midikraft::MidiController::makeOneHandle();
	bool timeoutCalled = false;
	controller.addMessageHandler(handle, [&timeoutCalled](juce::MidiInput*, juce::MidiMessage const&) {
		timeoutCalled = true;
	}, 100, midikraft::MidiController::TimeoutActivity::INCLUDE_PARTIAL_SYSEX);
	midikraft::MidiControllerTestAccess::setLastActivity(controller, handle, 100);

	auto timeoutHandler = midikraft::MidiControllerTestAccess::beginTimeoutDispatch(controller, 200);
	REQUIRE(timeoutHandler);
	midikraft::MidiControllerTestAccess::deliverPartialSysex(controller);
	timeoutHandler(nullptr, midikraft::MidiController::makeTimeoutMessage());

	CHECK(timeoutCalled);
}
