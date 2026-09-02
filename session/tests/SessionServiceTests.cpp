#include "FakeSessionService.h"

#include <iostream>
#include <string>
#include <vector>

namespace {
	using namespace midikraft::session;

	int failures = 0;

	void check(bool condition, char const* expression, int line) {
		if (!condition) {
			std::cerr << "line " << line << ": check failed: " << expression << '\n';
			++failures;
		}
	}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

	RequestContext context(std::string requestId, std::string clientId = "client-a", std::string pluginId = "plugin-a") {
		return { std::move(requestId), std::move(clientId), std::move(pluginId), std::nullopt };
	}

	SessionPatch patch(FakeSessionService& service, std::string patchId, std::string requestId) {
		auto result = service.getPatch({ context(std::move(requestId)), std::move(patchId) });
		CHECK(result);
		return result ? result.value().value : SessionPatch {};
	}

	ApplyToEditBufferRequest applyRequest(std::string requestId, std::string clientId, std::string pluginId,
		std::string synthId, SessionPatch selectedPatch) {
		return { context(std::move(requestId), std::move(clientId), std::move(pluginId)), std::move(synthId),
			selectedPatch.adaptationId, std::move(selectedPatch) };
	}

	void completeManualRecallWorkflowWithoutHardware() {
		FakeSessionService service;
		auto info = service.getServerInfo(context("info"));
		CHECK(info);
		if (info) CHECK(info.value().value.protocolMajor == CURRENT_SESSION_PROTOCOL_MAJOR);

		auto synths = service.listConfiguredSynthInstances({ context("synths"), { 2, std::nullopt } });
		CHECK(synths);
		if (synths) {
			CHECK(synths.value().value.items.size() == 2);
			CHECK(synths.value().value.nextPageToken == std::optional<std::string>("2"));
		}

		auto search = service.searchPatches({ context("search"), "warm", std::optional<std::string>("Oberheim Matrix 1000"), { 10, std::nullopt } });
		CHECK(search);
		CHECK(search && search.value().value.items.size() == 1);
		auto selected = patch(service, "patch-warm-bass", "get-patch");

		auto published = service.publishSession({ context("publish"), "Matrix Bass", "Test Host",
			{ "synth-matrix", "Oberheim Matrix 1000" }, selected.name, selected.fingerprint });
		CHECK(published);
		CHECK(published && published.value().value.sessions.size() == 1);

		auto applied = service.applyToEditBuffer(applyRequest("send", "client-a", "plugin-a", "synth-matrix", selected));
		CHECK(applied);
		CHECK(applied && applied.value().value.status.state == TransferState::Accepted);
		for (int step = 0; step != 4; ++step) service.advanceTransfers();

		auto status = service.getTransferStatus({ context("status"), applied.value().value.status.transferId });
		CHECK(status);
		if (status) {
			CHECK(status.value().value.status.state == TransferState::Succeeded);
			CHECK(status.value().value.status.verification == VerificationState::Unverified);
			CHECK(status.value().value.pluginInstanceName == "Matrix Bass");
		}
	}

	void multipleClientsAreVisibleAndAttributed() {
		FakeSessionService service;
		auto first = service.publishSession({ context("publish-a", "client-a", "plugin-a"), "Matrix Bass", "Host A",
			{ "synth-matrix", "Oberheim Matrix 1000" }, "Warm Bass", "fingerprint-a" });
		auto second = service.publishSession({ context("publish-b", "client-b", "plugin-b"), "Prophet Pad", "Host B",
			{ "synth-prophet", "Sequential Prophet-6" }, "Cloud Pad", "fingerprint-b" });
		CHECK(first);
		CHECK(second);
		CHECK(service.currentSnapshot().sessions.size() == 2);

		auto selected = patch(service, "patch-cloud-pad", "get-cloud");
		auto transfer = service.applyToEditBuffer(applyRequest("send-b", "client-b", "plugin-b", "synth-prophet", selected));
		CHECK(transfer);
		if (transfer) {
			CHECK(transfer.value().value.clientId == "client-b");
			CHECK(transfer.value().value.pluginInstanceId == "plugin-b");
			CHECK(transfer.value().value.pluginInstanceName == "Prophet Pad");
		}
	}

	void repeatedMutatingRequestIdsAreIdempotent() {
		FakeSessionService service;
		auto selected = patch(service, "patch-warm-bass", "get-for-idempotency");
		auto request = applyRequest("same-send", "client-a", "plugin-a", "synth-matrix", selected);
		auto first = service.applyToEditBuffer(request);
		service.advanceTransfers();
		auto second = service.applyToEditBuffer(request);
		CHECK(first);
		CHECK(second);
		CHECK(first && second && first.value().value.status.transferId == second.value().value.status.transferId);
		CHECK(second && second.value().value.status.state == TransferState::Preparing);
		CHECK(service.currentSnapshot().transfers.size() == 1);

		request.configuredSynthInstanceId = "synth-offline";
		auto conflictingReuse = service.applyToEditBuffer(request);
		CHECK(!conflictingReuse);
		if (!conflictingReuse) CHECK(conflictingReuse.error().code == ServiceErrorCode::InvalidRequest);
	}

	void sameDeviceOperationsNeverInterleave() {
		FakeSessionService service;
		auto selected = patch(service, "patch-warm-bass", "get-serialized");
		auto first = service.applyToEditBuffer(applyRequest("send-first", "client-a", "plugin-a", "synth-matrix", selected));
		auto second = service.applyToEditBuffer(applyRequest("send-second", "client-b", "plugin-b", "synth-matrix", selected));
		CHECK(first && second);
		CHECK(first && first.value().value.status.state == TransferState::Accepted);
		CHECK(second && second.value().value.status.state == TransferState::Queued);

		service.advanceTransfers();
		auto snapshot = service.currentSnapshot();
		CHECK(snapshot.transfers[0].status.state == TransferState::Preparing);
		CHECK(snapshot.transfers[1].status.state == TransferState::Queued);

		auto cancelled = service.cancelTransfer({ context("cancel-first"), first.value().value.status.transferId });
		CHECK(cancelled);
		snapshot = service.currentSnapshot();
		CHECK(snapshot.transfers[0].status.state == TransferState::Cancelled);
		CHECK(snapshot.transfers[1].status.state == TransferState::Accepted);
	}

	void differentDevicesProgressIndependently() {
		FakeSessionService service;
		auto matrixPatch = patch(service, "patch-warm-bass", "get-matrix-independent");
		auto prophetPatch = patch(service, "patch-cloud-pad", "get-prophet-independent");
		auto matrix = service.applyToEditBuffer(applyRequest("send-matrix", "client-a", "plugin-a", "synth-matrix", matrixPatch));
		auto prophet = service.applyToEditBuffer(applyRequest("send-prophet", "client-b", "plugin-b", "synth-prophet", prophetPatch));
		CHECK(matrix && prophet);
		CHECK(matrix && matrix.value().value.status.state == TransferState::Accepted);
		CHECK(prophet && prophet.value().value.status.state == TransferState::Accepted);

		service.advanceTransfers();
		auto snapshot = service.currentSnapshot();
		CHECK(snapshot.transfers[0].status.state == TransferState::Preparing);
		CHECK(snapshot.transfers[1].status.state == TransferState::Preparing);
	}

	void cancellationAndFailuresAreDeterministic() {
		FakeSessionService service;
		auto selected = patch(service, "patch-cloud-pad", "get-failure");
		service.setTransferBehavior("planned-failure", { TransferState::Sending,
			{ ServiceErrorCode::MidiPortBusy, "Fake MIDI port is busy", true } });
		auto failing = service.applyToEditBuffer(applyRequest("planned-failure", "client-a", "plugin-a", "synth-prophet", selected));
		CHECK(failing);
		service.advanceTransfers();
		service.advanceTransfers();
		auto snapshot = service.currentSnapshot();
		CHECK(snapshot.transfers[0].status.state == TransferState::Failed);
		CHECK(snapshot.transfers[0].error.has_value());
		if (snapshot.transfers[0].error) CHECK(snapshot.transfers[0].error->code == ServiceErrorCode::MidiPortBusy);

		auto matrixPatch = patch(service, "patch-warm-bass", "get-cancel");
		auto active = service.applyToEditBuffer(applyRequest("cancel-me", "client-a", "plugin-a", "synth-matrix", matrixPatch));
		CHECK(active);
		auto cancelled = service.cancelTransfer({ context("cancel-request"), active.value().value.status.transferId });
		auto repeated = service.cancelTransfer({ context("cancel-request"), active.value().value.status.transferId });
		CHECK(cancelled && repeated);
		CHECK(cancelled && cancelled.value().value.status.state == TransferState::Cancelled);
		CHECK(repeated && repeated.value().value.status.state == TransferState::Cancelled);
	}

	void progressAndVerificationAreDeterministic() {
		FakeSessionService service;
		auto selected = patch(service, "patch-cloud-pad", "get-progress");
		auto transfer = service.applyToEditBuffer(applyRequest("progress-send", "client-a", "plugin-a", "synth-prophet", selected));
		CHECK(transfer);
		std::vector<TransferState> states;
		std::vector<double> progress;
		for (int step = 0; step != 5; ++step) {
			service.advanceTransfers();
			auto const current = service.currentSnapshot().transfers[0].status;
			states.push_back(current.state);
			progress.push_back(current.progress.value_or(-1.0));
		}
		CHECK(states == std::vector<TransferState>({ TransferState::Preparing, TransferState::Sending,
			TransferState::Sending, TransferState::Verifying, TransferState::Succeeded }));
		CHECK(progress == std::vector<double>({ 0.1, 0.35, 0.8, 0.9, 1.0 }));
		CHECK(service.currentSnapshot().transfers[0].status.verification == VerificationState::Verified);
	}

	void observersCanDisconnectAndResubscribeToCurrentState() {
		FakeSessionService service;
		std::vector<SessionSnapshot> firstObserverUpdates;
		auto observerId = service.subscribe([&](SessionSnapshot const& snapshot) { firstObserverUpdates.push_back(snapshot); });
		CHECK(firstObserverUpdates.size() == 1);
		service.unsubscribe(observerId);

		auto publish = service.publishSession({ context("publish-while-away"), "Matrix Bass", std::nullopt,
			{ "synth-matrix", "Oberheim Matrix 1000" }, "Warm Bass", "stored-fingerprint" });
		CHECK(publish);
		CHECK(firstObserverUpdates.size() == 1);

		std::vector<SessionSnapshot> secondObserverUpdates;
		auto const secondObserverId = service.subscribe([&](SessionSnapshot const& snapshot) { secondObserverUpdates.push_back(snapshot); });
		(void) secondObserverId;
		CHECK(secondObserverUpdates.size() == 1);
		CHECK(secondObserverUpdates[0].sessions.size() == 1);
		CHECK(secondObserverUpdates[0].revision == service.currentSnapshot().revision);
	}

	void deadlinesAndOfflineSynthsHaveStableErrors() {
		FakeSessionService service;
		service.setNowUnixMillis(1000);
		auto expiredContext = context("expired");
		expiredContext.deadlineUnixMillis = 1000;
		auto expired = service.getServerInfo(expiredContext);
		CHECK(!expired);
		if (!expired) CHECK(expired.error().code == ServiceErrorCode::DeadlineExceeded);

		auto selected = patch(service, "patch-warm-bass", "get-offline");
		auto offline = service.applyToEditBuffer(applyRequest("offline-send", "client-a", "plugin-a", "synth-offline", selected));
		CHECK(!offline);
		if (!offline) CHECK(offline.error().code == ServiceErrorCode::SynthOffline);
	}
}

int main() {
	completeManualRecallWorkflowWithoutHardware();
	multipleClientsAreVisibleAndAttributed();
	repeatedMutatingRequestIdsAreIdempotent();
	sameDeviceOperationsNeverInterleave();
	differentDevicesProgressIndependently();
	cancellationAndFailuresAreDeterministic();
	progressAndVerificationAreDeterministic();
	observersCanDisconnectAndResubscribeToCurrentState();
	deadlinesAndOfflineSynthsHaveStableErrors();
	if (failures != 0) std::cerr << failures << " session service test(s) failed\n";
	return failures == 0 ? 0 : 1;
}
