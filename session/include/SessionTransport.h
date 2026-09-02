/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "SessionService.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace midikraft::session {

	inline constexpr std::uint32_t SESSION_IPC_MAX_FRAME_BYTES = 4U * 1024U * 1024U;

	namespace ipc_operation {
		inline constexpr std::string_view GET_SERVER_INFO = "getServerInfo";
		inline constexpr std::string_view LIST_CONFIGURED_SYNTH_INSTANCES = "listConfiguredSynthInstances";
		inline constexpr std::string_view GET_CONFIGURED_SYNTH_INSTANCE = "getConfiguredSynthInstance";
		inline constexpr std::string_view SEARCH_PATCHES = "searchPatches";
		inline constexpr std::string_view GET_PATCH = "getPatch";
		inline constexpr std::string_view APPLY_TO_EDIT_BUFFER = "applyToEditBuffer";
		inline constexpr std::string_view GET_TRANSFER_STATUS = "getTransferStatus";
		inline constexpr std::string_view CANCEL_TRANSFER = "cancelTransfer";
		inline constexpr std::string_view OPEN_KNOBKRAFT = "openKnobKraft";
		inline constexpr std::string_view PUBLISH_SESSION = "publishSession";
		inline constexpr std::string_view DISCONNECT_SESSION = "disconnectSession";
	}

	struct DiscoveryRecord {
		std::uint32_t protocolMajor = CURRENT_SESSION_PROTOCOL_MAJOR;
		std::uint32_t protocolMinor = CURRENT_SESSION_PROTOCOL_MINOR;
		std::uint16_t port = 0;
		std::uint64_t processId = 0;
		std::string generationId;
		std::string authenticationToken;
		std::int64_t writtenAtUnixMillis = 0;

		bool operator==(DiscoveryRecord const& other) const = default;
	};

	using UnixMillisProvider = std::function<std::int64_t()>;
	using TokenGenerator = std::function<std::string()>;

	class DiscoveryFile {
	public:
		explicit DiscoveryFile(std::filesystem::path path);

		[[nodiscard]] ServiceResult<DiscoveryRecord> read(std::int64_t nowUnixMillis,
			std::chrono::milliseconds maximumAge) const;
		[[nodiscard]] ServiceResult<bool> write(DiscoveryRecord const& record) const;
		void removeIfGenerationMatches(std::string const& generationId) const;
		[[nodiscard]] std::filesystem::path const& path() const noexcept;

	private:
		std::filesystem::path path_;
	};

	class FrameDecoder {
	public:
		explicit FrameDecoder(std::uint32_t maximumFrameBytes = SESSION_IPC_MAX_FRAME_BYTES);

		[[nodiscard]] ServiceResult<std::vector<std::string>> append(std::span<std::uint8_t const> bytes);
		void reset();

	private:
		std::uint32_t maximumFrameBytes_;
		std::vector<std::uint8_t> buffer_;
		std::optional<std::uint32_t> expectedPayloadBytes_;
	};

	[[nodiscard]] ServiceResult<std::vector<std::uint8_t>> frameMessage(
		std::string const& payload, std::uint32_t maximumFrameBytes = SESSION_IPC_MAX_FRAME_BYTES);

	struct IpcResponse {
		std::string requestId;
		std::optional<std::string> payloadJson;
		std::optional<ServiceError> error;

		[[nodiscard]] bool hasValue() const noexcept { return payloadJson.has_value() && !error.has_value(); }
	};

	struct SessionIpcServerConfig {
		std::shared_ptr<DiscoveryFile> discoveryFile;
		UnixMillisProvider nowUnixMillis;
		TokenGenerator tokenGenerator;
		std::chrono::milliseconds staleClientTimeout { 5'000 };
		std::uint32_t maximumFrameBytes = SESSION_IPC_MAX_FRAME_BYTES;
		std::size_t maximumQueuedRequests = 256;
		std::size_t maximumClients = 64;
	};

	// The server owns a single service executor. SessionService implementations
	// therefore do not need to be internally thread-safe, even with many clients.
	class SessionIpcServer {
	public:
		SessionIpcServer(SessionService& service, SessionIpcServerConfig config);
		~SessionIpcServer();

		SessionIpcServer(SessionIpcServer const&) = delete;
		SessionIpcServer& operator=(SessionIpcServer const&) = delete;

		[[nodiscard]] ServiceResult<DiscoveryRecord> start();
		void stop();
		[[nodiscard]] bool isRunning() const noexcept;
		[[nodiscard]] std::optional<DiscoveryRecord> discoveryRecord() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};

	struct SessionIpcClientConfig {
		std::shared_ptr<DiscoveryFile> discoveryFile;
		UnixMillisProvider nowUnixMillis;
		std::chrono::milliseconds maximumDiscoveryAge { 10'000 };
		std::chrono::milliseconds reconnectDelay { 100 };
		std::chrono::milliseconds heartbeatInterval { 500 };
		std::uint32_t maximumFrameBytes = SESSION_IPC_MAX_FRAME_BYTES;
		std::size_t maximumPendingRequests = 128;
	};

	// All socket activity occurs on the owned worker. Calls are queued and their
	// futures complete on that worker; callers never need to block a UI thread.
	class SessionIpcClient {
	public:
		using SnapshotObserver = std::function<void(SessionSnapshot const&)>;
		using ConnectionObserver = std::function<void(bool)>;

		explicit SessionIpcClient(SessionIpcClientConfig config);
		~SessionIpcClient();

		SessionIpcClient(SessionIpcClient const&) = delete;
		SessionIpcClient& operator=(SessionIpcClient const&) = delete;

		void start();
		void stop();
		[[nodiscard]] bool isConnected() const noexcept;
		void setSnapshotObserver(SnapshotObserver observer);
		void setConnectionObserver(ConnectionObserver observer);

		// bodyJson must be a JSON object. Mutating operations can be retried with
		// the same request ID after a lost connection; the service owns idempotency.
		[[nodiscard]] std::future<IpcResponse> request(std::string operation, RequestContext context,
			std::string bodyJson = "{}");
		void cancelPending(std::string const& requestId);

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};

}
