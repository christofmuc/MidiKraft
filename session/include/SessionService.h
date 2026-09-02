/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "SessionTypes.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace midikraft::session {

	inline constexpr std::uint32_t CURRENT_SESSION_PROTOCOL_MAJOR = 1;
	inline constexpr std::uint32_t CURRENT_SESSION_PROTOCOL_MINOR = 0;

	enum class ServiceErrorCode {
		Unavailable,
		ProtocolIncompatible,
		AuthenticationFailed,
		InvalidRequest,
		DeadlineExceeded,
		ConfiguredSynthMissing,
		SynthOffline,
		MidiPortBusy,
		PatchIncompatible,
		PatchDataInvalid,
		AdaptationError,
		TransferTimedOut,
		TransferCancelled,
		VerificationMismatch,
		PatchNotFound,
		TransferNotFound,
		CancelNotAllowed,
		NavigationUnavailable,
		InternalError
	};

	struct ServiceError {
		ServiceErrorCode code = ServiceErrorCode::InternalError;
		std::string message;
		bool retryable = false;

		bool operator==(ServiceError const& other) const = default;
	};

	template<typename T>
	class ServiceResult {
	public:
		static ServiceResult success(T value) {
			return ServiceResult(std::move(value), std::nullopt);
		}

		static ServiceResult failure(ServiceError error) {
			return ServiceResult(std::nullopt, std::move(error));
		}

		[[nodiscard]] bool hasValue() const noexcept { return value_.has_value(); }
		[[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }
		[[nodiscard]] T const& value() const& { return value_.value(); }
		[[nodiscard]] T& value() & { return value_.value(); }
		[[nodiscard]] T&& value() && { return std::move(value_).value(); }
		[[nodiscard]] ServiceError const& error() const { return error_.value(); }

	private:
		ServiceResult(std::optional<T> value, std::optional<ServiceError> error)
			: value_(std::move(value)), error_(std::move(error)) {}

		std::optional<T> value_;
		std::optional<ServiceError> error_;
	};

	struct RequestContext {
		std::string requestId;
		std::string clientId;
		std::string pluginInstanceId;
		// UTC Unix time in milliseconds. A request whose deadline is equal to
		// the service's current time is already expired.
		std::optional<std::int64_t> deadlineUnixMillis;

		bool operator==(RequestContext const& other) const = default;
	};

	struct PageRequest {
		std::uint32_t pageSize = 50;
		std::optional<std::string> pageToken;

		bool operator==(PageRequest const& other) const = default;
	};

	struct ServerInfo {
		std::string productName;
		std::string productVersion;
		std::string generationId;
		std::uint32_t protocolMajor = CURRENT_SESSION_PROTOCOL_MAJOR;
		std::uint32_t protocolMinor = CURRENT_SESSION_PROTOCOL_MINOR;

		bool operator==(ServerInfo const& other) const = default;
	};

	struct SessionSynthCapabilities {
		bool editBuffer = false;
		bool programDump = false;
		bool customProgramChange = false;
		bool verification = false;

		bool operator==(SessionSynthCapabilities const& other) const = default;
	};

	// This is a service projection, not the persistent configured-synth model
	// owned by the application. Keeping it transport-neutral lets WP-02 map its
	// richer model here without creating two competing identity definitions.
	struct SessionSynthInfo {
		std::string configuredSynthInstanceId;
		std::string displayName;
		std::string adaptationId;
		bool online = false;
		SessionSynthCapabilities capabilities;

		bool operator==(SessionSynthInfo const& other) const = default;
	};

	struct PatchSummary {
		std::string patchId;
		std::string adaptationId;
		std::string dataTypeId;
		std::string name;
		std::string fingerprint;
		std::optional<PatchProvenance> source;

		bool operator==(PatchSummary const& other) const = default;
	};

	template<typename T>
	struct ServiceResponse {
		std::string requestId;
		T value;

		bool operator==(ServiceResponse const& other) const = default;
	};

	template<typename T>
	struct PagedItems {
		std::vector<T> items;
		std::optional<std::string> nextPageToken;

		bool operator==(PagedItems const& other) const = default;
	};

	struct ListSynthsRequest {
		RequestContext context;
		PageRequest page;
	};

	struct GetSynthRequest {
		RequestContext context;
		std::string configuredSynthInstanceId;
	};

	struct SearchPatchesRequest {
		RequestContext context;
		std::string query;
		std::optional<std::string> adaptationId;
		PageRequest page;
	};

	struct GetPatchRequest {
		RequestContext context;
		std::string patchId;
	};

	struct ApplyToEditBufferRequest {
		RequestContext context;
		std::string configuredSynthInstanceId;
		std::string expectedAdaptationId;
		SessionPatch patch;
	};

	struct GetTransferStatusRequest {
		RequestContext context;
		std::string transferId;
	};

	struct CancelTransferRequest {
		RequestContext context;
		std::string transferId;
	};

	enum class NavigationTargetKind {
		Application,
		ConfiguredSynth,
		Patch
	};

	struct OpenKnobKraftRequest {
		RequestContext context;
		NavigationTargetKind target = NavigationTargetKind::Application;
		std::optional<std::string> targetId;
	};

	struct NavigationResult {
		bool accepted = false;
	};

	struct PluginSessionState {
		std::string clientId;
		std::string pluginInstanceId;
		std::string instanceName;
		std::optional<std::string> hostName;
		SynthBinding binding;
		std::optional<std::string> storedPatchName;
		std::optional<std::string> storedPatchFingerprint;
		std::int64_t lastSeenUnixMillis = 0;

		bool operator==(PluginSessionState const& other) const = default;
	};

	struct PublishSessionRequest {
		RequestContext context;
		std::string instanceName;
		std::optional<std::string> hostName;
		SynthBinding binding;
		std::optional<std::string> storedPatchName;
		std::optional<std::string> storedPatchFingerprint;
	};

	struct DisconnectSessionRequest {
		RequestContext context;
	};

	struct TransferRecord {
		TransferStatus status;
		std::string clientId;
		std::string pluginInstanceId;
		std::string pluginInstanceName;
		std::string configuredSynthInstanceId;
		std::string patchName;
		std::string patchFingerprint;
		std::optional<ServiceError> error;
		std::int64_t updatedAtUnixMillis = 0;

		bool operator==(TransferRecord const& other) const = default;
	};

	struct SessionSnapshot {
		std::uint64_t revision = 0;
		std::vector<PluginSessionState> sessions;
		std::vector<TransferRecord> transfers;

		bool operator==(SessionSnapshot const& other) const = default;
	};

	using ObserverId = std::uint64_t;
	using SessionObserver = std::function<void(SessionSnapshot const&)>;

	class SessionService {
	public:
		virtual ~SessionService() = default;

		[[nodiscard]] virtual ServiceResult<ServiceResponse<ServerInfo>> getServerInfo(RequestContext const& request) = 0;
		[[nodiscard]] virtual ServiceResult<ServiceResponse<PagedItems<SessionSynthInfo>>> listConfiguredSynthInstances(ListSynthsRequest const& request) = 0;
		[[nodiscard]] virtual ServiceResult<ServiceResponse<SessionSynthInfo>> getConfiguredSynthInstance(GetSynthRequest const& request) = 0;
		[[nodiscard]] virtual ServiceResult<ServiceResponse<PagedItems<PatchSummary>>> searchPatches(SearchPatchesRequest const& request) = 0;
		[[nodiscard]] virtual ServiceResult<ServiceResponse<SessionPatch>> getPatch(GetPatchRequest const& request) = 0;
		[[nodiscard]] virtual ServiceResult<ServiceResponse<TransferRecord>> applyToEditBuffer(ApplyToEditBufferRequest const& request) = 0;
		[[nodiscard]] virtual ServiceResult<ServiceResponse<TransferRecord>> getTransferStatus(GetTransferStatusRequest const& request) = 0;
		[[nodiscard]] virtual ServiceResult<ServiceResponse<TransferRecord>> cancelTransfer(CancelTransferRequest const& request) = 0;
		[[nodiscard]] virtual ServiceResult<ServiceResponse<NavigationResult>> openKnobKraft(OpenKnobKraftRequest const& request) = 0;

		// Publishing is an idempotent register/update/heartbeat operation. It
		// deliberately carries UI-neutral state so both plugin and application
		// view models can consume the same snapshots.
		[[nodiscard]] virtual ServiceResult<ServiceResponse<SessionSnapshot>> publishSession(PublishSessionRequest const& request) = 0;
		[[nodiscard]] virtual ServiceResult<ServiceResponse<SessionSnapshot>> disconnectSession(DisconnectSessionRequest const& request) = 0;
		[[nodiscard]] virtual SessionSnapshot currentSnapshot() const = 0;
		// With emitCurrent enabled, subscribing always emits a complete snapshot
		// before any later updates. Observers therefore need no event replay.
		[[nodiscard]] virtual ObserverId subscribe(SessionObserver observer, bool emitCurrent = true) = 0;
		virtual void unsubscribe(ObserverId observerId) = 0;
	};

}
