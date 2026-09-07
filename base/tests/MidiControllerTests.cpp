#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "MidiController.h"

namespace midikraft {

	struct MidiControllerTestAccess {
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

	class MidiControllerFixture {
	public:
		MidiControllerFixture() : controller_(new midikraft::MidiController()) {
			midikraft::MidiControllerTestAccess::stopTimer(*controller_);
		}

		~MidiControllerFixture() {
			midikraft::MidiController::shutdown();
		}

		midikraft::MidiController& controller() const {
			return *controller_;
		}

	private:
		midikraft::MidiController* controller_;
	};

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
