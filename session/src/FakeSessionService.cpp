/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "FakeSessionService.h"

#include "SessionCodecs.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <deque>
#include <map>
#include <sstream>
#include <string_view>

namespace midikraft::session {

	namespace {
		constexpr std::uint32_t MAX_PAGE_SIZE = 100;

		ServiceError invalidRequest(std::string message) {
			return { ServiceErrorCode::InvalidRequest, std::move(message), false };
		}

		std::string lowerCase(std::string_view input) {
			std::string result(input);
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
				return static_cast<char>(std::tolower(character));
			});
			return result;
		}

		std::string optionalText(std::optional<std::string> const& value) {
			return value.value_or("-");
		}

		std::string applySignature(ApplyToEditBufferRequest const& request) {
			return request.context.clientId + "\n" + request.context.pluginInstanceId + "\n"
				+ request.configuredSynthInstanceId + "\n" + request.expectedAdaptationId + "\n"
				+ request.patch.name + "\n" + request.patch.fingerprint;
		}

		std::string publishSignature(PublishSessionRequest const& request) {
			return request.context.clientId + "\n" + request.context.pluginInstanceId + "\n" + request.instanceName + "\n"
				+ optionalText(request.hostName) + "\n" + optionalText(request.binding.configuredSynthInstanceId) + "\n"
				+ optionalText(request.binding.fallbackAdaptationId) + "\n" + optionalText(request.storedPatchName) + "\n"
				+ optionalText(request.storedPatchFingerprint);
		}

		std::string navigationSignature(OpenKnobKraftRequest const& request) {
			return std::to_string(static_cast<int>(request.target)) + "\n" + optionalText(request.targetId);
		}

		template<typename T>
		ServiceResult<ServiceResponse<T>> response(std::string requestId, T value) {
			return ServiceResult<ServiceResponse<T>>::success({ std::move(requestId), std::move(value) });
		}

		template<typename T>
		ServiceResult<ServiceResponse<T>> failure(ServiceError error) {
			return ServiceResult<ServiceResponse<T>>::failure(std::move(error));
		}
	}

	struct FakeSessionService::Impl {
		struct PatchEntry {
			std::string patchId;
			SessionPatch patch;
		};

		struct TransferEntry {
			TransferRecord record;
			FakeTransferBehavior behavior;
			bool supportsVerification = false;
		};

		struct MutationEntry {
			std::string operation;
			std::string signature;
			std::optional<std::string> transferId;
		};

		std::int64_t now = 1'700'000'000'000;
		ServerInfo serverInfo { "KnobKraft Fake Session Service", "1.0-test", "fake-generation-1" };
		std::vector<SessionSynthInfo> synths;
		std::vector<PatchEntry> patches;
		std::map<std::string, PluginSessionState> sessions;
		std::vector<TransferEntry> transfers;
		std::map<std::string, std::deque<std::string>> deviceQueues;
		std::map<std::string, MutationEntry> mutations;
		std::map<std::string, FakeTransferBehavior> plannedBehaviors;
		std::map<ObserverId, SessionObserver> observers;
		ObserverId nextObserverId = 1;
		std::uint64_t nextTransferId = 1;
		std::uint64_t revision = 0;
		bool navigationAvailable = true;

		Impl() {
			synths = {
				{ "synth-matrix", "Studio Matrix-1000", "Oberheim Matrix 1000", true, { true, true, false, false } },
				{ "synth-prophet", "Desk Prophet-6", "Sequential Prophet-6", true, { true, true, false, true } },
				{ "synth-offline", "Touring Matrix-1000", "Oberheim Matrix 1000", false, { true, true, false, false } }
			};

			patches.push_back({ "patch-warm-bass", makePatch("Oberheim Matrix 1000", "Warm Bass", { 0xf0, 0x01, 0x02, 0xf7 }, 23) });
			patches.push_back({ "patch-brass-stack", makePatch("Oberheim Matrix 1000", "Brass Stack", { 0xf0, 0x03, 0x04, 0xf7 }, 24) });
			patches.push_back({ "patch-cloud-pad", makePatch("Sequential Prophet-6", "Cloud Pad", { 0xf0, 0x05, 0x06, 0xf7 }, 7) });
		}

		static SessionPatch makePatch(std::string adaptationId, std::string name, std::vector<std::uint8_t> payload, std::int32_t program) {
			SessionPatch patch;
			patch.adaptationId = std::move(adaptationId);
			patch.dataTypeId = "single-program";
			patch.name = std::move(name);
			patch.payload = std::move(payload);
			patch.source = PatchProvenance { std::nullopt, 0, program };
			auto fingerprint = SessionPatchCodec::fingerprint(patch);
			if (fingerprint) patch.fingerprint = fingerprint.value();
			return patch;
		}

		std::optional<ServiceError> validate(RequestContext const& context) const {
			if (context.requestId.empty()) return invalidRequest("requestId must not be empty");
			if (context.deadlineUnixMillis && *context.deadlineUnixMillis <= now)
				return ServiceError { ServiceErrorCode::DeadlineExceeded, "Request deadline has elapsed", false };
			return std::nullopt;
		}

		std::optional<std::size_t> pageOffset(PageRequest const& page) const {
			if (page.pageSize == 0 || page.pageSize > MAX_PAGE_SIZE) return std::nullopt;
			if (!page.pageToken) return 0;
			std::size_t value = 0;
			auto const begin = page.pageToken->data();
			auto const end = begin + page.pageToken->size();
			auto const parsed = std::from_chars(begin, end, value);
			if (parsed.ec != std::errc() || parsed.ptr != end) return std::nullopt;
			return value;
		}

		template<typename T>
		PagedItems<T> page(std::vector<T> const& items, std::size_t offset, std::uint32_t pageSize) const {
			PagedItems<T> result;
			if (offset >= items.size()) return result;
			auto const count = std::min<std::size_t>(pageSize, items.size() - offset);
			result.items.insert(result.items.end(), items.begin() + static_cast<std::ptrdiff_t>(offset),
				items.begin() + static_cast<std::ptrdiff_t>(offset + count));
			if (offset + count < items.size()) result.nextPageToken = std::to_string(offset + count);
			return result;
		}

		SessionSynthInfo const* findSynth(std::string const& instanceId) const {
			auto found = std::find_if(synths.begin(), synths.end(), [&](auto const& synth) {
				return synth.configuredSynthInstanceId == instanceId;
			});
			return found == synths.end() ? nullptr : &*found;
		}

		PatchEntry const* findPatch(std::string const& patchId) const {
			auto found = std::find_if(patches.begin(), patches.end(), [&](auto const& patch) { return patch.patchId == patchId; });
			return found == patches.end() ? nullptr : &*found;
		}

		TransferEntry* findTransfer(std::string const& transferId) {
			auto found = std::find_if(transfers.begin(), transfers.end(), [&](auto const& transfer) {
				return transfer.record.status.transferId == transferId;
			});
			return found == transfers.end() ? nullptr : &*found;
		}

		TransferEntry const* findTransfer(std::string const& transferId) const {
			auto found = std::find_if(transfers.begin(), transfers.end(), [&](auto const& transfer) {
				return transfer.record.status.transferId == transferId;
			});
			return found == transfers.end() ? nullptr : &*found;
		}

		SessionSnapshot snapshot() const {
			SessionSnapshot result;
			result.revision = revision;
			for (auto const& [clientId, session] : sessions) {
				(void) clientId;
				result.sessions.push_back(session);
			}
			for (auto const& transfer : transfers) result.transfers.push_back(transfer.record);
			return result;
		}

		void changed() {
			++revision;
			auto const current = snapshot();
			auto const observersCopy = observers;
			for (auto const& [observerId, observer] : observersCopy) {
				(void) observerId;
				if (observer) observer(current);
			}
		}

		MutationEntry const* priorMutation(std::string const& requestId) const {
			auto const found = mutations.find(requestId);
			return found == mutations.end() ? nullptr : &found->second;
		}

		void promoteNext(std::string const& deviceId) {
			auto queue = deviceQueues.find(deviceId);
			if (queue == deviceQueues.end() || queue->second.empty()) return;
			auto* next = findTransfer(queue->second.front());
			if (next && next->record.status.state == TransferState::Queued) {
				next->record.status.state = TransferState::Accepted;
				next->record.status.detail = "Accepted by fake device queue";
				next->record.updatedAtUnixMillis = now;
			}
		}

		void removeFromQueue(TransferEntry& transfer) {
			auto queue = deviceQueues.find(transfer.record.configuredSynthInstanceId);
			if (queue == deviceQueues.end()) return;
			auto const transferId = transfer.record.status.transferId;
			auto const wasActive = !queue->second.empty() && queue->second.front() == transferId;
			queue->second.erase(std::remove(queue->second.begin(), queue->second.end(), transferId), queue->second.end());
			if (wasActive) promoteNext(transfer.record.configuredSynthInstanceId);
		}

		bool failIfPlanned(TransferEntry& transfer, TransferState enteringState) {
			if (!transfer.behavior.failAt || *transfer.behavior.failAt != enteringState) return false;
			transfer.record.status.state = TransferState::Failed;
			transfer.record.status.detail = transfer.behavior.failure.message;
			transfer.record.error = transfer.behavior.failure;
			transfer.record.updatedAtUnixMillis = now;
			removeFromQueue(transfer);
			return true;
		}
	};

	FakeSessionService::FakeSessionService()
		: impl_(std::make_unique<Impl>()) {}

	FakeSessionService::~FakeSessionService() = default;

	ServiceResult<ServiceResponse<ServerInfo>> FakeSessionService::getServerInfo(RequestContext const& request) {
		if (auto error = impl_->validate(request)) return failure<ServerInfo>(*error);
		return response(request.requestId, impl_->serverInfo);
	}

	ServiceResult<ServiceResponse<PagedItems<SessionSynthInfo>>> FakeSessionService::listConfiguredSynthInstances(ListSynthsRequest const& request) {
		if (auto error = impl_->validate(request.context)) return failure<PagedItems<SessionSynthInfo>>(*error);
		auto offset = impl_->pageOffset(request.page);
		if (!offset) return failure<PagedItems<SessionSynthInfo>>(invalidRequest("Invalid page size or page token"));
		return response(request.context.requestId, impl_->page(impl_->synths, *offset, request.page.pageSize));
	}

	ServiceResult<ServiceResponse<SessionSynthInfo>> FakeSessionService::getConfiguredSynthInstance(GetSynthRequest const& request) {
		if (auto error = impl_->validate(request.context)) return failure<SessionSynthInfo>(*error);
		auto const* synth = impl_->findSynth(request.configuredSynthInstanceId);
		if (!synth) return failure<SessionSynthInfo>({ ServiceErrorCode::ConfiguredSynthMissing, "Configured synth was not found", false });
		return response(request.context.requestId, *synth);
	}

	ServiceResult<ServiceResponse<PagedItems<PatchSummary>>> FakeSessionService::searchPatches(SearchPatchesRequest const& request) {
		if (auto error = impl_->validate(request.context)) return failure<PagedItems<PatchSummary>>(*error);
		auto offset = impl_->pageOffset(request.page);
		if (!offset) return failure<PagedItems<PatchSummary>>(invalidRequest("Invalid page size or page token"));
		auto const query = lowerCase(request.query);
		std::vector<PatchSummary> matches;
		for (auto const& entry : impl_->patches) {
			if (request.adaptationId && entry.patch.adaptationId != *request.adaptationId) continue;
			if (!query.empty() && lowerCase(entry.patch.name).find(query) == std::string::npos) continue;
			matches.push_back({ entry.patchId, entry.patch.adaptationId, entry.patch.dataTypeId, entry.patch.name,
				entry.patch.fingerprint, entry.patch.source });
		}
		return response(request.context.requestId, impl_->page(matches, *offset, request.page.pageSize));
	}

	ServiceResult<ServiceResponse<SessionPatch>> FakeSessionService::getPatch(GetPatchRequest const& request) {
		if (auto error = impl_->validate(request.context)) return failure<SessionPatch>(*error);
		auto const* patch = impl_->findPatch(request.patchId);
		if (!patch) return failure<SessionPatch>({ ServiceErrorCode::PatchNotFound, "Patch was not found", false });
		return response(request.context.requestId, patch->patch);
	}

	ServiceResult<ServiceResponse<TransferRecord>> FakeSessionService::applyToEditBuffer(ApplyToEditBufferRequest const& request) {
		if (auto error = impl_->validate(request.context)) return failure<TransferRecord>(*error);
		if (request.context.clientId.empty() || request.context.pluginInstanceId.empty())
			return failure<TransferRecord>(invalidRequest("clientId and pluginInstanceId must not be empty"));

		auto const signature = applySignature(request);
		if (auto const* previous = impl_->priorMutation(request.context.requestId)) {
			if (previous->operation != "apply" || previous->signature != signature)
				return failure<TransferRecord>(invalidRequest("requestId was already used for a different mutation"));
			auto* existing = previous->transferId ? impl_->findTransfer(*previous->transferId) : nullptr;
			if (!existing) return failure<TransferRecord>({ ServiceErrorCode::InternalError, "Idempotent transfer record is missing", false });
			return response(request.context.requestId, existing->record);
		}

		auto const* synth = impl_->findSynth(request.configuredSynthInstanceId);
		if (!synth) return failure<TransferRecord>({ ServiceErrorCode::ConfiguredSynthMissing, "Configured synth was not found", false });
		if (!synth->online) return failure<TransferRecord>({ ServiceErrorCode::SynthOffline, "Configured synth is offline", true });
		if (!synth->capabilities.editBuffer)
			return failure<TransferRecord>({ ServiceErrorCode::PatchIncompatible, "Configured synth has no edit-buffer capability", false });
		if (request.expectedAdaptationId != synth->adaptationId || request.patch.adaptationId != synth->adaptationId)
			return failure<TransferRecord>({ ServiceErrorCode::PatchIncompatible, "Patch adaptation does not match configured synth", false });
		auto fingerprint = SessionPatchCodec::fingerprint(request.patch);
		if (!fingerprint || fingerprint.value() != request.patch.fingerprint)
			return failure<TransferRecord>({ ServiceErrorCode::PatchDataInvalid, "Patch fingerprint is invalid", false });

		auto transferId = std::string("fake-transfer-") + std::to_string(impl_->nextTransferId++);
		auto& queue = impl_->deviceQueues[request.configuredSynthInstanceId];
		auto const initialState = queue.empty() ? TransferState::Accepted : TransferState::Queued;
		std::string instanceName = request.context.pluginInstanceId;
		if (auto session = impl_->sessions.find(request.context.clientId); session != impl_->sessions.end()) instanceName = session->second.instanceName;

		TransferRecord record;
		record.status.transferId = transferId;
		record.status.requestId = request.context.requestId;
		record.status.state = initialState;
		record.status.verification = VerificationState::NotAttempted;
		record.status.progress = 0.0;
		record.status.detail = initialState == TransferState::Accepted ? "Accepted by fake device queue" : "Queued behind another transfer";
		record.clientId = request.context.clientId;
		record.pluginInstanceId = request.context.pluginInstanceId;
		record.pluginInstanceName = std::move(instanceName);
		record.configuredSynthInstanceId = request.configuredSynthInstanceId;
		record.patchName = request.patch.name;
		record.patchFingerprint = request.patch.fingerprint;
		record.updatedAtUnixMillis = impl_->now;

		auto behavior = impl_->plannedBehaviors.find(request.context.requestId);
		impl_->transfers.push_back({ record, behavior == impl_->plannedBehaviors.end() ? FakeTransferBehavior {} : behavior->second,
			synth->capabilities.verification });
		queue.push_back(transferId);
		impl_->mutations.emplace(request.context.requestId, Impl::MutationEntry { "apply", signature, transferId });
		impl_->changed();
		return response(request.context.requestId, record);
	}

	ServiceResult<ServiceResponse<TransferRecord>> FakeSessionService::getTransferStatus(GetTransferStatusRequest const& request) {
		if (auto error = impl_->validate(request.context)) return failure<TransferRecord>(*error);
		auto const* transfer = impl_->findTransfer(request.transferId);
		if (!transfer) return failure<TransferRecord>({ ServiceErrorCode::TransferNotFound, "Transfer was not found", false });
		return response(request.context.requestId, transfer->record);
	}

	ServiceResult<ServiceResponse<TransferRecord>> FakeSessionService::cancelTransfer(CancelTransferRequest const& request) {
		if (auto error = impl_->validate(request.context)) return failure<TransferRecord>(*error);
		auto const signature = request.context.clientId + "\n" + request.transferId;
		if (auto const* previous = impl_->priorMutation(request.context.requestId)) {
			if (previous->operation != "cancel" || previous->signature != signature)
				return failure<TransferRecord>(invalidRequest("requestId was already used for a different mutation"));
			auto* existing = previous->transferId ? impl_->findTransfer(*previous->transferId) : nullptr;
			if (!existing) return failure<TransferRecord>({ ServiceErrorCode::TransferNotFound, "Transfer was not found", false });
			return response(request.context.requestId, existing->record);
		}

		auto* transfer = impl_->findTransfer(request.transferId);
		if (!transfer) return failure<TransferRecord>({ ServiceErrorCode::TransferNotFound, "Transfer was not found", false });
		if (transfer->record.status.state == TransferState::Succeeded || transfer->record.status.state == TransferState::Failed
			|| transfer->record.status.state == TransferState::Cancelled)
			return failure<TransferRecord>({ ServiceErrorCode::CancelNotAllowed, "Transfer is already complete", false });

		impl_->mutations.emplace(request.context.requestId, Impl::MutationEntry { "cancel", signature, request.transferId });
		transfer->record.status.state = TransferState::Cancelled;
		transfer->record.status.detail = "Cancelled by client";
		transfer->record.error = ServiceError { ServiceErrorCode::TransferCancelled, "Transfer was cancelled", false };
		transfer->record.updatedAtUnixMillis = impl_->now;
		impl_->removeFromQueue(*transfer);
		auto const result = transfer->record;
		impl_->changed();
		return response(request.context.requestId, result);
	}

	ServiceResult<ServiceResponse<NavigationResult>> FakeSessionService::openKnobKraft(OpenKnobKraftRequest const& request) {
		if (auto error = impl_->validate(request.context)) return failure<NavigationResult>(*error);
		auto const signature = navigationSignature(request);
		if (auto const* previous = impl_->priorMutation(request.context.requestId)) {
			if (previous->operation != "open" || previous->signature != signature)
				return failure<NavigationResult>(invalidRequest("requestId was already used for a different mutation"));
			return response(request.context.requestId, NavigationResult { true });
		}
		if (!impl_->navigationAvailable)
			return failure<NavigationResult>({ ServiceErrorCode::NavigationUnavailable, "Navigation is unavailable", true });
		impl_->mutations.emplace(request.context.requestId, Impl::MutationEntry { "open", signature, std::nullopt });
		return response(request.context.requestId, NavigationResult { true });
	}

	ServiceResult<ServiceResponse<SessionSnapshot>> FakeSessionService::publishSession(PublishSessionRequest const& request) {
		if (auto error = impl_->validate(request.context)) return failure<SessionSnapshot>(*error);
		if (request.context.clientId.empty() || request.context.pluginInstanceId.empty() || request.instanceName.empty())
			return failure<SessionSnapshot>(invalidRequest("clientId, pluginInstanceId, and instanceName must not be empty"));
		auto const signature = publishSignature(request);
		if (auto const* previous = impl_->priorMutation(request.context.requestId)) {
			if (previous->operation != "publish" || previous->signature != signature)
				return failure<SessionSnapshot>(invalidRequest("requestId was already used for a different mutation"));
			return response(request.context.requestId, impl_->snapshot());
		}

		impl_->mutations.emplace(request.context.requestId, Impl::MutationEntry { "publish", signature, std::nullopt });
		impl_->sessions[request.context.clientId] = PluginSessionState { request.context.clientId, request.context.pluginInstanceId,
			request.instanceName, request.hostName, request.binding, request.storedPatchName, request.storedPatchFingerprint, impl_->now };
		impl_->changed();
		return response(request.context.requestId, impl_->snapshot());
	}

	ServiceResult<ServiceResponse<SessionSnapshot>> FakeSessionService::disconnectSession(DisconnectSessionRequest const& request) {
		if (auto error = impl_->validate(request.context)) return failure<SessionSnapshot>(*error);
		if (request.context.clientId.empty()) return failure<SessionSnapshot>(invalidRequest("clientId must not be empty"));
		auto const signature = request.context.clientId;
		if (auto const* previous = impl_->priorMutation(request.context.requestId)) {
			if (previous->operation != "disconnect" || previous->signature != signature)
				return failure<SessionSnapshot>(invalidRequest("requestId was already used for a different mutation"));
			return response(request.context.requestId, impl_->snapshot());
		}
		impl_->mutations.emplace(request.context.requestId, Impl::MutationEntry { "disconnect", signature, std::nullopt });
		auto const removed = impl_->sessions.erase(request.context.clientId);
		if (removed != 0) impl_->changed();
		return response(request.context.requestId, impl_->snapshot());
	}

	SessionSnapshot FakeSessionService::currentSnapshot() const {
		return impl_->snapshot();
	}

	ObserverId FakeSessionService::subscribe(SessionObserver observer, bool emitCurrent) {
		auto const observerId = impl_->nextObserverId++;
		impl_->observers.emplace(observerId, observer);
		if (emitCurrent && observer) observer(impl_->snapshot());
		return observerId;
	}

	void FakeSessionService::unsubscribe(ObserverId observerId) {
		impl_->observers.erase(observerId);
	}

	void FakeSessionService::setNowUnixMillis(std::int64_t nowUnixMillis) {
		impl_->now = nowUnixMillis;
	}

	std::int64_t FakeSessionService::nowUnixMillis() const {
		return impl_->now;
	}

	void FakeSessionService::advanceTransfers() {
		std::vector<std::string> activeTransfers;
		for (auto const& [deviceId, queue] : impl_->deviceQueues) {
			(void) deviceId;
			if (!queue.empty()) activeTransfers.push_back(queue.front());
		}
		if (activeTransfers.empty()) return;

		for (auto const& transferId : activeTransfers) {
			auto* transfer = impl_->findTransfer(transferId);
			if (!transfer) continue;
			auto& status = transfer->record.status;
			switch (status.state) {
			case TransferState::Accepted:
				if (impl_->failIfPlanned(*transfer, TransferState::Preparing)) break;
				status.state = TransferState::Preparing;
				status.progress = 0.1;
				status.detail = "Preparing fake device messages";
				break;
			case TransferState::Preparing:
				if (impl_->failIfPlanned(*transfer, TransferState::Sending)) break;
				status.state = TransferState::Sending;
				status.progress = 0.35;
				status.detail = "Sending fake device messages";
				break;
			case TransferState::Sending:
				if (status.progress.value_or(0.0) < 0.8) {
					status.progress = 0.8;
					status.detail = "Sending fake device messages";
					break;
				}
				if (transfer->supportsVerification) {
					if (impl_->failIfPlanned(*transfer, TransferState::Verifying)) break;
					status.state = TransferState::Verifying;
					status.progress = 0.9;
					status.detail = "Verifying fake hardware state";
				} else {
					if (impl_->failIfPlanned(*transfer, TransferState::Succeeded)) break;
					status.state = TransferState::Succeeded;
					status.verification = VerificationState::Unverified;
					status.progress = 1.0;
					status.detail = "Sent without verification";
					impl_->removeFromQueue(*transfer);
				}
				break;
			case TransferState::Verifying:
				if (impl_->failIfPlanned(*transfer, TransferState::Succeeded)) break;
				status.state = TransferState::Succeeded;
				status.verification = VerificationState::Verified;
				status.progress = 1.0;
				status.detail = "Sent and verified";
				impl_->removeFromQueue(*transfer);
				break;
			case TransferState::Queued:
			case TransferState::Succeeded:
			case TransferState::Failed:
			case TransferState::Cancelled:
				break;
			}
			transfer->record.updatedAtUnixMillis = impl_->now;
		}
		impl_->changed();
	}

	void FakeSessionService::setTransferBehavior(std::string requestId, FakeTransferBehavior behavior) {
		impl_->plannedBehaviors[requestId] = behavior;
		if (auto const* mutation = impl_->priorMutation(requestId); mutation && mutation->transferId) {
			if (auto* transfer = impl_->findTransfer(*mutation->transferId)) transfer->behavior = std::move(behavior);
		}
	}

	void FakeSessionService::setNavigationAvailable(bool available) {
		impl_->navigationAvailable = available;
	}

}
