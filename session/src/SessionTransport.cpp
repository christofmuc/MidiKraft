/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SessionTransport.h"

#include "SessionCodecs.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <Windows.h>
#include <bcrypt.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace midikraft::session {

	using Json = nlohmann::json;

	namespace {
#ifdef _WIN32
		using SocketHandle = SOCKET;
		constexpr SocketHandle INVALID_SOCKET_HANDLE = INVALID_SOCKET;
#else
		using SocketHandle = int;
		constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;
#endif

		std::int64_t systemNowUnixMillis() {
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
		}

		std::uint64_t currentProcessId() {
#ifdef _WIN32
			return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
			return static_cast<std::uint64_t>(getpid());
#endif
		}

		std::string randomToken() {
			std::array<unsigned char, 32> bytes {};
			bool generated = false;
#ifdef _WIN32
			generated = BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
				BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
			std::ifstream randomSource("/dev/urandom", std::ios::binary);
			if (randomSource) {
				randomSource.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
				generated = randomSource.good();
			}
#endif
			if (!generated) {
				std::random_device fallback;
				for (auto& byte : bytes) byte = static_cast<unsigned char>(fallback());
			}
			std::ostringstream result;
			result << std::hex << std::setfill('0');
			for (auto byte : bytes) result << std::setw(2) << static_cast<unsigned int>(byte);
			return result.str();
		}

		ServiceError transportError(ServiceErrorCode code, std::string message, bool retryable = false) {
			return { code, std::move(message), retryable };
		}

		void closeSocket(SocketHandle socket) {
			if (socket == INVALID_SOCKET_HANDLE) return;
#ifdef _WIN32
			shutdown(socket, SD_BOTH);
			closesocket(socket);
#else
			shutdown(socket, SHUT_RDWR);
			close(socket);
#endif
		}

		bool socketRuntimeReady() {
#ifdef _WIN32
			static bool const ready = [] {
				WSADATA data {};
				return WSAStartup(MAKEWORD(2, 2), &data) == 0;
			}();
			return ready;
#else
			return true;
#endif
		}

		bool sendAll(SocketHandle socket, std::span<std::uint8_t const> bytes) {
			std::size_t sent = 0;
			while (sent < bytes.size()) {
				auto const remaining = bytes.size() - sent;
				auto const chunk = static_cast<int>(std::min<std::size_t>(remaining, 64U * 1024U));
#ifdef _WIN32
				auto const result = send(socket, reinterpret_cast<char const*>(bytes.data() + sent), chunk, 0);
#else
				auto const result = send(socket, bytes.data() + sent, static_cast<std::size_t>(chunk), MSG_NOSIGNAL);
#endif
				if (result <= 0) return false;
				sent += static_cast<std::size_t>(result);
			}
			return true;
		}

		bool sendJson(SocketHandle socket, std::mutex& mutex, Json const& message, std::uint32_t maximumFrameBytes) {
			auto framed = frameMessage(message.dump(), maximumFrameBytes);
			if (!framed) return false;
			std::lock_guard lock(mutex);
			return sendAll(socket, framed.value());
		}

		void writeError(Json& destination, ServiceError const& error) {
			destination = Json { { "code", static_cast<int>(error.code) }, { "message", error.message }, { "retryable", error.retryable } };
		}

		ServiceError readError(Json const& source) {
			return { static_cast<ServiceErrorCode>(source.at("code").get<int>()), source.at("message").get<std::string>(),
				source.value("retryable", false) };
		}

		Json contextJson(RequestContext const& context) {
			Json result { { "requestId", context.requestId }, { "clientId", context.clientId },
				{ "pluginInstanceId", context.pluginInstanceId } };
			if (context.deadlineUnixMillis) result["deadlineUnixMillis"] = *context.deadlineUnixMillis;
			return result;
		}

		RequestContext readContext(Json const& source) {
			RequestContext result;
			result.requestId = source.at("requestId").get<std::string>();
			result.clientId = source.value("clientId", std::string {});
			result.pluginInstanceId = source.value("pluginInstanceId", std::string {});
			if (source.contains("deadlineUnixMillis")) result.deadlineUnixMillis = source.at("deadlineUnixMillis").get<std::int64_t>();
			return result;
		}

		Json provenanceJson(PatchProvenance const& provenance) {
			Json result = Json::object();
			if (provenance.databaseId) result["databaseId"] = *provenance.databaseId;
			if (provenance.bank) result["bank"] = *provenance.bank;
			if (provenance.program) result["program"] = *provenance.program;
			return result;
		}

		Json patchJson(SessionPatch const& patch) {
			auto encoded = SessionPatchCodec::encode(patch);
			if (!encoded) throw std::runtime_error(encoded.error().message);
			return Json::parse(encoded.value());
		}

		SessionPatch readPatch(Json const& source) {
			auto decoded = SessionPatchCodec::decode(source.dump());
			if (!decoded) throw std::runtime_error(decoded.error().message);
			return decoded.value();
		}

		Json bindingJson(SynthBinding const& binding) {
			Json result = Json::object();
			if (binding.configuredSynthInstanceId) result["configuredSynthInstanceId"] = *binding.configuredSynthInstanceId;
			if (binding.fallbackAdaptationId) result["fallbackAdaptationId"] = *binding.fallbackAdaptationId;
			return result;
		}

		SynthBinding readBinding(Json const& source) {
			SynthBinding result;
			if (source.contains("configuredSynthInstanceId")) result.configuredSynthInstanceId = source.at("configuredSynthInstanceId").get<std::string>();
			if (source.contains("fallbackAdaptationId")) result.fallbackAdaptationId = source.at("fallbackAdaptationId").get<std::string>();
			return result;
		}

		Json synthJson(SessionSynthInfo const& synth) {
			return { { "configuredSynthInstanceId", synth.configuredSynthInstanceId }, { "displayName", synth.displayName },
				{ "adaptationId", synth.adaptationId }, { "online", synth.online },
				{ "capabilities", { { "editBuffer", synth.capabilities.editBuffer }, { "programDump", synth.capabilities.programDump },
					{ "customProgramChange", synth.capabilities.customProgramChange }, { "verification", synth.capabilities.verification } } } };
		}

		Json patchSummaryJson(PatchSummary const& patch) {
			Json result { { "patchId", patch.patchId }, { "adaptationId", patch.adaptationId }, { "dataTypeId", patch.dataTypeId },
				{ "name", patch.name }, { "fingerprint", patch.fingerprint } };
			if (patch.source) result["source"] = provenanceJson(*patch.source);
			return result;
		}

		Json transferStatusJson(TransferStatus const& status) {
			Json result { { "transferId", status.transferId }, { "requestId", status.requestId },
				{ "state", static_cast<int>(status.state) }, { "verification", static_cast<int>(status.verification) },
				{ "detail", status.detail } };
			if (status.progress) result["progress"] = *status.progress;
			return result;
		}

		Json transferJson(TransferRecord const& transfer) {
			Json result { { "status", transferStatusJson(transfer.status) }, { "clientId", transfer.clientId },
				{ "pluginInstanceId", transfer.pluginInstanceId }, { "pluginInstanceName", transfer.pluginInstanceName },
				{ "configuredSynthInstanceId", transfer.configuredSynthInstanceId }, { "patchName", transfer.patchName },
				{ "patchFingerprint", transfer.patchFingerprint }, { "updatedAtUnixMillis", transfer.updatedAtUnixMillis } };
			if (transfer.error) writeError(result["error"], *transfer.error);
			return result;
		}

		Json snapshotJson(SessionSnapshot const& snapshot) {
			Json sessions = Json::array();
			for (auto const& session : snapshot.sessions) {
				Json value { { "clientId", session.clientId }, { "pluginInstanceId", session.pluginInstanceId },
					{ "instanceName", session.instanceName }, { "binding", bindingJson(session.binding) },
					{ "lastSeenUnixMillis", session.lastSeenUnixMillis } };
				if (session.hostName) value["hostName"] = *session.hostName;
				if (session.storedPatchName) value["storedPatchName"] = *session.storedPatchName;
				if (session.storedPatchFingerprint) value["storedPatchFingerprint"] = *session.storedPatchFingerprint;
				sessions.push_back(std::move(value));
			}
			Json transfers = Json::array();
			for (auto const& transfer : snapshot.transfers) transfers.push_back(transferJson(transfer));
			return { { "revision", snapshot.revision }, { "sessions", std::move(sessions) }, { "transfers", std::move(transfers) } };
		}

		TransferStatus readTransferStatus(Json const& source) {
			TransferStatus result;
			result.transferId = source.at("transferId").get<std::string>();
			result.requestId = source.at("requestId").get<std::string>();
			result.state = static_cast<TransferState>(source.at("state").get<int>());
			result.verification = static_cast<VerificationState>(source.at("verification").get<int>());
			if (source.contains("progress")) result.progress = source.at("progress").get<double>();
			result.detail = source.value("detail", std::string {});
			return result;
		}

		TransferRecord readTransfer(Json const& source) {
			TransferRecord result;
			result.status = readTransferStatus(source.at("status"));
			result.clientId = source.value("clientId", std::string {});
			result.pluginInstanceId = source.value("pluginInstanceId", std::string {});
			result.pluginInstanceName = source.value("pluginInstanceName", std::string {});
			result.configuredSynthInstanceId = source.value("configuredSynthInstanceId", std::string {});
			result.patchName = source.value("patchName", std::string {});
			result.patchFingerprint = source.value("patchFingerprint", std::string {});
			if (source.contains("error")) result.error = readError(source.at("error"));
			result.updatedAtUnixMillis = source.value("updatedAtUnixMillis", std::int64_t {});
			return result;
		}

		SessionSnapshot readSnapshot(Json const& source) {
			SessionSnapshot result;
			result.revision = source.at("revision").get<std::uint64_t>();
			for (auto const& value : source.at("sessions")) {
				PluginSessionState session;
				session.clientId = value.value("clientId", std::string {});
				session.pluginInstanceId = value.value("pluginInstanceId", std::string {});
				session.instanceName = value.value("instanceName", std::string {});
				if (value.contains("hostName")) session.hostName = value.at("hostName").get<std::string>();
				session.binding = readBinding(value.at("binding"));
				if (value.contains("storedPatchName")) session.storedPatchName = value.at("storedPatchName").get<std::string>();
				if (value.contains("storedPatchFingerprint")) session.storedPatchFingerprint = value.at("storedPatchFingerprint").get<std::string>();
				session.lastSeenUnixMillis = value.value("lastSeenUnixMillis", std::int64_t {});
				result.sessions.push_back(std::move(session));
			}
			for (auto const& value : source.at("transfers")) result.transfers.push_back(readTransfer(value));
			return result;
		}

		PageRequest readPage(Json const& body) {
			PageRequest result;
			result.pageSize = body.value("pageSize", std::uint32_t { 50 });
			if (body.contains("pageToken")) result.pageToken = body.at("pageToken").get<std::string>();
			return result;
		}

		template<typename T>
		Json serviceReply(ServiceResult<ServiceResponse<T>> const& result, std::function<Json(T const&)> encode) {
			Json response { { "kind", "response" }, { "protocolMajor", CURRENT_SESSION_PROTOCOL_MAJOR },
				{ "protocolMinor", CURRENT_SESSION_PROTOCOL_MINOR } };
			if (result) {
				response["requestId"] = result.value().requestId;
				response["ok"] = true;
				response["body"] = encode(result.value().value);
			} else {
				response["ok"] = false;
				writeError(response["error"], result.error());
			}
			return response;
		}

		Json dispatch(SessionService& service, std::string const& operation, RequestContext const& context, Json const& body) {
			if (operation == "getServerInfo") {
				return serviceReply<ServerInfo>(service.getServerInfo(context), [](ServerInfo const& info) {
					return Json { { "productName", info.productName }, { "productVersion", info.productVersion },
						{ "generationId", info.generationId }, { "protocolMajor", info.protocolMajor }, { "protocolMinor", info.protocolMinor } };
				});
			}
			if (operation == "listConfiguredSynthInstances") {
				return serviceReply<PagedItems<SessionSynthInfo>>(service.listConfiguredSynthInstances({ context, readPage(body) }), [](auto const& page) {
					Json items = Json::array();
					for (auto const& item : page.items) items.push_back(synthJson(item));
					Json result { { "items", std::move(items) } };
					if (page.nextPageToken) result["nextPageToken"] = *page.nextPageToken;
					return result;
				});
			}
			if (operation == "getConfiguredSynthInstance") {
				return serviceReply<SessionSynthInfo>(service.getConfiguredSynthInstance({ context,
					body.at("configuredSynthInstanceId").get<std::string>() }), synthJson);
			}
			if (operation == "searchPatches") {
				std::optional<std::string> adaptation;
				if (body.contains("adaptationId")) adaptation = body.at("adaptationId").get<std::string>();
				return serviceReply<PagedItems<PatchSummary>>(service.searchPatches({ context, body.value("query", std::string {}),
					adaptation, readPage(body) }), [](auto const& page) {
					Json items = Json::array();
					for (auto const& item : page.items) items.push_back(patchSummaryJson(item));
					Json result { { "items", std::move(items) } };
					if (page.nextPageToken) result["nextPageToken"] = *page.nextPageToken;
					return result;
				});
			}
			if (operation == "getPatch") {
				return serviceReply<SessionPatch>(service.getPatch({ context, body.at("patchId").get<std::string>() }), patchJson);
			}
			if (operation == "applyToEditBuffer") {
				return serviceReply<TransferRecord>(service.applyToEditBuffer({ context,
					body.at("configuredSynthInstanceId").get<std::string>(), body.at("expectedAdaptationId").get<std::string>(),
					readPatch(body.at("patch")) }), transferJson);
			}
			if (operation == "getTransferStatus") {
				return serviceReply<TransferRecord>(service.getTransferStatus({ context, body.at("transferId").get<std::string>() }), transferJson);
			}
			if (operation == "cancelTransfer") {
				return serviceReply<TransferRecord>(service.cancelTransfer({ context, body.at("transferId").get<std::string>() }), transferJson);
			}
			if (operation == "openKnobKraft") {
				std::optional<std::string> targetId;
				if (body.contains("targetId")) targetId = body.at("targetId").get<std::string>();
				return serviceReply<NavigationResult>(service.openKnobKraft({ context,
					static_cast<NavigationTargetKind>(body.value("target", 0)), targetId }), [](NavigationResult const& result) {
					return Json { { "accepted", result.accepted } };
				});
			}
			if (operation == "publishSession") {
				std::optional<std::string> hostName;
				std::optional<std::string> patchName;
				std::optional<std::string> fingerprint;
				if (body.contains("hostName")) hostName = body.at("hostName").get<std::string>();
				if (body.contains("storedPatchName")) patchName = body.at("storedPatchName").get<std::string>();
				if (body.contains("storedPatchFingerprint")) fingerprint = body.at("storedPatchFingerprint").get<std::string>();
				return serviceReply<SessionSnapshot>(service.publishSession({ context, body.at("instanceName").get<std::string>(),
					hostName, readBinding(body.at("binding")), patchName, fingerprint }), snapshotJson);
			}
			if (operation == "disconnectSession") {
				return serviceReply<SessionSnapshot>(service.disconnectSession({ context }), snapshotJson);
			}
			throw std::runtime_error("Unknown operation");
		}

		Json errorResponse(std::string requestId, ServiceError error) {
			Json response { { "kind", "response" }, { "protocolMajor", CURRENT_SESSION_PROTOCOL_MAJOR },
				{ "protocolMinor", CURRENT_SESSION_PROTOCOL_MINOR }, { "requestId", std::move(requestId) }, { "ok", false } };
			writeError(response["error"], error);
			return response;
		}
	}

	DiscoveryFile::DiscoveryFile(std::filesystem::path path) : path_(std::move(path)) {}

	ServiceResult<DiscoveryRecord> DiscoveryFile::read(std::int64_t nowUnixMillis,
		std::chrono::milliseconds maximumAge) const {
		try {
			std::ifstream input(path_, std::ios::binary);
			if (!input) return ServiceResult<DiscoveryRecord>::failure(transportError(ServiceErrorCode::Unavailable,
				"KnobKraft discovery file is unavailable", true));
			std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			if (contents.size() > 64U * 1024U) return ServiceResult<DiscoveryRecord>::failure(
				transportError(ServiceErrorCode::InvalidRequest, "Discovery file is oversized"));
			auto const json = Json::parse(contents);
			DiscoveryRecord result;
			result.protocolMajor = json.at("protocolMajor").get<std::uint32_t>();
			result.protocolMinor = json.at("protocolMinor").get<std::uint32_t>();
			result.port = json.at("port").get<std::uint16_t>();
			result.processId = json.at("processId").get<std::uint64_t>();
			result.generationId = json.at("generationId").get<std::string>();
			result.authenticationToken = json.at("authenticationToken").get<std::string>();
			result.writtenAtUnixMillis = json.at("writtenAtUnixMillis").get<std::int64_t>();
			if (result.port == 0 || result.generationId.empty() || result.authenticationToken.size() < 16)
				return ServiceResult<DiscoveryRecord>::failure(transportError(ServiceErrorCode::InvalidRequest, "Discovery data is invalid"));
			if (result.protocolMajor != CURRENT_SESSION_PROTOCOL_MAJOR)
				return ServiceResult<DiscoveryRecord>::failure(transportError(ServiceErrorCode::ProtocolIncompatible,
					"Discovery protocol major version is incompatible"));
			if (nowUnixMillis < result.writtenAtUnixMillis || nowUnixMillis - result.writtenAtUnixMillis > maximumAge.count())
				return ServiceResult<DiscoveryRecord>::failure(transportError(ServiceErrorCode::Unavailable,
					"KnobKraft discovery file is stale", true));
			return ServiceResult<DiscoveryRecord>::success(std::move(result));
		} catch (std::exception const&) {
			return ServiceResult<DiscoveryRecord>::failure(transportError(ServiceErrorCode::InvalidRequest,
				"KnobKraft discovery file is malformed"));
		}
	}

	ServiceResult<bool> DiscoveryFile::write(DiscoveryRecord const& record) const {
		try {
			auto parent = path_.parent_path();
			if (!parent.empty()) std::filesystem::create_directories(parent);
			auto temporary = path_;
			temporary += ".tmp-" + record.generationId;
			Json json { { "protocolMajor", record.protocolMajor }, { "protocolMinor", record.protocolMinor },
				{ "port", record.port }, { "processId", record.processId }, { "generationId", record.generationId },
				{ "authenticationToken", record.authenticationToken }, { "writtenAtUnixMillis", record.writtenAtUnixMillis } };
			{
				std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
				if (!output) return ServiceResult<bool>::failure(transportError(ServiceErrorCode::Unavailable,
					"Cannot write KnobKraft discovery file", true));
				output << json.dump();
				output.flush();
				if (!output) return ServiceResult<bool>::failure(transportError(ServiceErrorCode::Unavailable,
					"Cannot finish KnobKraft discovery file", true));
			}
#ifndef _WIN32
			chmod(temporary.string().c_str(), S_IRUSR | S_IWUSR);
#endif
			std::error_code error;
			std::filesystem::remove(path_, error);
			error.clear();
			std::filesystem::rename(temporary, path_, error);
			if (error) return ServiceResult<bool>::failure(transportError(ServiceErrorCode::Unavailable,
				"Cannot publish KnobKraft discovery file", true));
			return ServiceResult<bool>::success(true);
		} catch (std::exception const&) {
			return ServiceResult<bool>::failure(transportError(ServiceErrorCode::Unavailable,
				"Cannot publish KnobKraft discovery file", true));
		}
	}

	void DiscoveryFile::removeIfGenerationMatches(std::string const& generationId) const {
		try {
			std::ifstream input(path_, std::ios::binary);
			if (!input) return;
			Json const json = Json::parse(input);
			if (json.value("generationId", std::string {}) != generationId) return;
			input.close();
			std::error_code error;
			std::filesystem::remove(path_, error);
		} catch (std::exception const&) {
		}
	}

	std::filesystem::path const& DiscoveryFile::path() const noexcept { return path_; }

	FrameDecoder::FrameDecoder(std::uint32_t maximumFrameBytes) : maximumFrameBytes_(maximumFrameBytes) {}

	ServiceResult<std::vector<std::string>> FrameDecoder::append(std::span<std::uint8_t const> bytes) {
		std::vector<std::string> messages;
		std::size_t offset = 0;
		while (offset < bytes.size()) {
			if (!expectedPayloadBytes_) {
				auto const headerBytes = std::min<std::size_t>(4U - buffer_.size(), bytes.size() - offset);
				buffer_.insert(buffer_.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset),
					bytes.begin() + static_cast<std::ptrdiff_t>(offset + headerBytes));
				offset += headerBytes;
				if (buffer_.size() < 4) continue;
				auto const size = (static_cast<std::uint32_t>(buffer_[0]) << 24U) | (static_cast<std::uint32_t>(buffer_[1]) << 16U)
					| (static_cast<std::uint32_t>(buffer_[2]) << 8U) | static_cast<std::uint32_t>(buffer_[3]);
				buffer_.clear();
				if (size == 0 || size > maximumFrameBytes_) {
					reset();
					return ServiceResult<std::vector<std::string>>::failure(transportError(ServiceErrorCode::InvalidRequest,
						"Frame length is invalid"));
				}
				expectedPayloadBytes_ = size;
			}
			auto const payloadBytes = std::min<std::size_t>(*expectedPayloadBytes_ - buffer_.size(), bytes.size() - offset);
			buffer_.insert(buffer_.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset),
				bytes.begin() + static_cast<std::ptrdiff_t>(offset + payloadBytes));
			offset += payloadBytes;
			if (buffer_.size() == *expectedPayloadBytes_) {
				messages.emplace_back(reinterpret_cast<char const*>(buffer_.data()), buffer_.size());
				buffer_.clear();
				expectedPayloadBytes_.reset();
			}
		}
		return ServiceResult<std::vector<std::string>>::success(std::move(messages));
	}

	void FrameDecoder::reset() { buffer_.clear(); expectedPayloadBytes_.reset(); }

	ServiceResult<std::vector<std::uint8_t>> frameMessage(std::string const& payload, std::uint32_t maximumFrameBytes) {
		if (payload.empty() || payload.size() > maximumFrameBytes)
			return ServiceResult<std::vector<std::uint8_t>>::failure(transportError(ServiceErrorCode::InvalidRequest,
				"Frame payload exceeds limit"));
		auto const size = static_cast<std::uint32_t>(payload.size());
		std::vector<std::uint8_t> result;
		result.reserve(payload.size() + 4U);
		result.push_back(static_cast<std::uint8_t>((size >> 24U) & 0xffU));
		result.push_back(static_cast<std::uint8_t>((size >> 16U) & 0xffU));
		result.push_back(static_cast<std::uint8_t>((size >> 8U) & 0xffU));
		result.push_back(static_cast<std::uint8_t>(size & 0xffU));
		result.insert(result.end(), payload.begin(), payload.end());
		return ServiceResult<std::vector<std::uint8_t>>::success(std::move(result));
	}

	struct SessionIpcServer::Impl {
		struct ClientConnection {
			SocketHandle socket = INVALID_SOCKET_HANDLE;
			std::mutex writeMutex;
			std::atomic<std::int64_t> lastSeen { 0 };
			std::atomic<bool> open { true };
			std::mutex identityMutex;
			std::string clientId;
			std::string pluginInstanceId;
		};

		SessionService& service;
		SessionIpcServerConfig config;
		UnixMillisProvider now;
		TokenGenerator token;
		std::atomic<bool> running { false };
		SocketHandle listener = INVALID_SOCKET_HANDLE;
		std::optional<DiscoveryRecord> discovery;
		std::thread acceptThread;
		std::thread serviceThread;
		std::thread staleThread;
		std::mutex clientsMutex;
		std::vector<std::shared_ptr<ClientConnection>> clients;
		std::vector<std::thread> clientThreads;
		std::mutex tasksMutex;
		std::condition_variable tasksChanged;
		std::deque<std::function<void()>> tasks;
		ObserverId serviceObserverId = 0;

		Impl(SessionService& target, SessionIpcServerConfig value)
			: service(target), config(std::move(value)), now(config.nowUnixMillis ? config.nowUnixMillis : systemNowUnixMillis),
			token(config.tokenGenerator ? config.tokenGenerator : randomToken) {}

		~Impl() { stop(); }

		bool enqueue(std::function<void()> task, bool essential = false) {
			{
				std::lock_guard lock(tasksMutex);
				if (!essential && tasks.size() >= config.maximumQueuedRequests) return false;
				tasks.push_back(std::move(task));
			}
			tasksChanged.notify_one();
			return true;
		}

		void serviceLoop() {
			serviceObserverId = service.subscribe([this](SessionSnapshot const& snapshot) { broadcastSnapshot(snapshot); });
			for (;;) {
				std::function<void()> task;
				{
					std::unique_lock lock(tasksMutex);
					tasksChanged.wait(lock, [this] { return !running.load() || !tasks.empty(); });
					if (!running.load() && tasks.empty()) break;
					task = std::move(tasks.front());
					tasks.pop_front();
				}
				try { task(); } catch (std::exception const&) {}
			}
			service.unsubscribe(serviceObserverId);
			serviceObserverId = 0;
		}

		void broadcastSnapshot(SessionSnapshot const& snapshot) {
			Json event { { "kind", "event" }, { "event", "sessionSnapshot" },
				{ "protocolMajor", CURRENT_SESSION_PROTOCOL_MAJOR }, { "protocolMinor", CURRENT_SESSION_PROTOCOL_MINOR },
				{ "body", snapshotJson(snapshot) } };
			std::vector<std::shared_ptr<ClientConnection>> copy;
			{
				std::lock_guard lock(clientsMutex);
				copy = clients;
			}
			for (auto const& client : copy) {
				if (client->open.load() && !sendJson(client->socket, client->writeMutex, event, config.maximumFrameBytes))
					client->open.store(false);
			}
		}

		void disconnectPublishedSession(std::shared_ptr<ClientConnection> const& client) {
			std::string clientId;
			std::string pluginId;
			{
				std::lock_guard lock(client->identityMutex);
				clientId = client->clientId;
				pluginId = client->pluginInstanceId;
				client->clientId.clear();
			}
			if (clientId.empty()) return;
			(void) enqueue([this, clientId = std::move(clientId), pluginId = std::move(pluginId)] {
				RequestContext context { "transport-disconnect-" + token(), clientId, pluginId, std::nullopt };
				(void) service.disconnectSession({ std::move(context) });
			}, true);
		}

		void closeClient(std::shared_ptr<ClientConnection> const& client) {
			if (!client->open.exchange(false)) return;
			closeSocket(client->socket);
			disconnectPublishedSession(client);
		}

		void clientLoop(std::shared_ptr<ClientConnection> client) {
			FrameDecoder decoder(config.maximumFrameBytes);
			std::array<std::uint8_t, 16U * 1024U> received {};
			while (running.load() && client->open.load()) {
#ifdef _WIN32
				auto const count = recv(client->socket, reinterpret_cast<char*>(received.data()), static_cast<int>(received.size()), 0);
#else
				auto const count = recv(client->socket, received.data(), received.size(), 0);
#endif
				if (count <= 0) break;
				client->lastSeen.store(now());
				auto decoded = decoder.append(std::span<std::uint8_t const>(received.data(), static_cast<std::size_t>(count)));
				if (!decoded) break;
				for (auto const& payload : decoded.value()) {
					Json message;
					std::string requestId;
					try {
						message = Json::parse(payload);
						requestId = message.value("requestId", std::string {});
						if (!message.is_object() || message.value("kind", std::string {}) != "request")
							throw std::runtime_error("Expected request envelope");
						if (!discovery || message.value("token", std::string {}) != discovery->authenticationToken) {
							auto response = errorResponse(requestId, transportError(ServiceErrorCode::AuthenticationFailed,
								"Authentication failed"));
							(void) sendJson(client->socket, client->writeMutex, response, config.maximumFrameBytes);
							client->open.store(false);
							break;
						}
						if (message.value("protocolMajor", 0U) != CURRENT_SESSION_PROTOCOL_MAJOR) {
							auto response = errorResponse(requestId, transportError(ServiceErrorCode::ProtocolIncompatible,
								"Protocol major version is incompatible"));
							(void) sendJson(client->socket, client->writeMutex, response, config.maximumFrameBytes);
							continue;
						}
						if (message.value("operation", std::string {}) == "heartbeat") {
							Json response { { "kind", "heartbeat" }, { "protocolMajor", CURRENT_SESSION_PROTOCOL_MAJOR },
								{ "protocolMinor", CURRENT_SESSION_PROTOCOL_MINOR } };
							(void) sendJson(client->socket, client->writeMutex, response, config.maximumFrameBytes);
							continue;
						}
						auto const operation = message.at("operation").get<std::string>();
						auto const context = readContext(message.at("context"));
						if (context.requestId != requestId) throw std::runtime_error("Envelope request IDs differ");
						if (operation == "publishSession") {
							std::lock_guard lock(client->identityMutex);
							client->clientId = context.clientId;
							client->pluginInstanceId = context.pluginInstanceId;
						}
						auto body = message.value("body", Json::object());
						auto const queued = enqueue([this, client, operation, context, body = std::move(body)] {
							Json response;
							try {
								if (context.deadlineUnixMillis && *context.deadlineUnixMillis <= now()) {
									response = errorResponse(context.requestId, transportError(ServiceErrorCode::DeadlineExceeded,
										"IPC request deadline elapsed"));
								} else {
									response = dispatch(service, operation, context, body);
								}
							} catch (std::exception const&) {
								response = errorResponse(context.requestId, transportError(ServiceErrorCode::InvalidRequest,
									"Request body or operation is invalid"));
							}
							(void) sendJson(client->socket, client->writeMutex, response, config.maximumFrameBytes);
						});
						if (!queued) {
							auto response = errorResponse(context.requestId, transportError(ServiceErrorCode::Unavailable,
								"IPC service queue is full", true));
							(void) sendJson(client->socket, client->writeMutex, response, config.maximumFrameBytes);
						}
					} catch (std::exception const&) {
						auto response = errorResponse(requestId, transportError(ServiceErrorCode::InvalidRequest,
							"Request envelope is malformed"));
						(void) sendJson(client->socket, client->writeMutex, response, config.maximumFrameBytes);
					}
				}
			}
			closeClient(client);
		}

		void acceptLoop() {
			while (running.load()) {
				sockaddr_in address {};
#ifdef _WIN32
				int addressSize = sizeof(address);
#else
				socklen_t addressSize = sizeof(address);
#endif
				auto accepted = accept(listener, reinterpret_cast<sockaddr*>(&address), &addressSize);
				if (accepted == INVALID_SOCKET_HANDLE) {
					if (!running.load()) break;
					continue;
				}
				if (ntohl(address.sin_addr.s_addr) != INADDR_LOOPBACK) {
					closeSocket(accepted);
					continue;
				}
				auto client = std::make_shared<ClientConnection>();
				client->socket = accepted;
				client->lastSeen.store(now());
				{
					std::lock_guard lock(clientsMutex);
					auto const activeClients = std::count_if(clients.begin(), clients.end(), [](auto const& existing) {
						return existing->open.load();
					});
					if (activeClients >= static_cast<std::ptrdiff_t>(config.maximumClients)) {
						closeSocket(accepted);
						continue;
					}
					clients.push_back(client);
					clientThreads.emplace_back([this, client] { clientLoop(client); });
				}
			}
		}

		void staleLoop() {
			auto nextDiscoveryRefresh = std::chrono::steady_clock::now() + std::chrono::seconds(1);
			while (running.load()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				auto const current = now();
				if (discovery && config.discoveryFile && std::chrono::steady_clock::now() >= nextDiscoveryRefresh) {
					auto refreshed = *discovery;
					refreshed.writtenAtUnixMillis = current;
					(void) config.discoveryFile->write(refreshed);
					nextDiscoveryRefresh = std::chrono::steady_clock::now() + std::chrono::seconds(1);
				}
				std::vector<std::shared_ptr<ClientConnection>> copy;
				{
					std::lock_guard lock(clientsMutex);
					copy = clients;
				}
				for (auto const& client : copy) {
					if (client->open.load() && current - client->lastSeen.load() > config.staleClientTimeout.count()) closeClient(client);
				}
			}
		}

		ServiceResult<DiscoveryRecord> start() {
			if (running.exchange(true)) return ServiceResult<DiscoveryRecord>::failure(
				transportError(ServiceErrorCode::InvalidRequest, "IPC server is already running"));
			if (!config.discoveryFile || !socketRuntimeReady()) {
				running.store(false);
				return ServiceResult<DiscoveryRecord>::failure(transportError(ServiceErrorCode::Unavailable,
					"IPC runtime is unavailable"));
			}
			listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (listener == INVALID_SOCKET_HANDLE) {
				running.store(false);
				return ServiceResult<DiscoveryRecord>::failure(transportError(ServiceErrorCode::Unavailable,
					"Cannot create IPC listener", true));
			}
			sockaddr_in address {};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			address.sin_port = 0;
			if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(listener, 16) != 0) {
				closeSocket(listener);
				listener = INVALID_SOCKET_HANDLE;
				running.store(false);
				return ServiceResult<DiscoveryRecord>::failure(transportError(ServiceErrorCode::Unavailable,
					"Cannot bind loopback IPC listener", true));
			}
#ifdef _WIN32
			int addressSize = sizeof(address);
#else
			socklen_t addressSize = sizeof(address);
#endif
			if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressSize) != 0) {
				stop();
				return ServiceResult<DiscoveryRecord>::failure(transportError(ServiceErrorCode::Unavailable,
					"Cannot determine IPC listener port", true));
			}
			DiscoveryRecord record;
			record.port = ntohs(address.sin_port);
			record.processId = currentProcessId();
			record.generationId = token();
			record.authenticationToken = token();
			record.writtenAtUnixMillis = now();
			auto written = config.discoveryFile->write(record);
			if (!written) {
				stop();
				return ServiceResult<DiscoveryRecord>::failure(written.error());
			}
			discovery = record;
			serviceThread = std::thread([this] { serviceLoop(); });
			acceptThread = std::thread([this] { acceptLoop(); });
			staleThread = std::thread([this] { staleLoop(); });
			return ServiceResult<DiscoveryRecord>::success(record);
		}

		void stop() {
			if (!running.exchange(false)) return;
			closeSocket(listener);
			listener = INVALID_SOCKET_HANDLE;
			std::vector<std::shared_ptr<ClientConnection>> copy;
			{
				std::lock_guard lock(clientsMutex);
				copy = clients;
			}
			for (auto const& client : copy) closeClient(client);
			tasksChanged.notify_all();
			if (acceptThread.joinable()) acceptThread.join();
			if (staleThread.joinable()) staleThread.join();
			for (auto& thread : clientThreads) if (thread.joinable()) thread.join();
			if (serviceThread.joinable()) serviceThread.join();
			clientThreads.clear();
			clients.clear();
			if (discovery && config.discoveryFile) config.discoveryFile->removeIfGenerationMatches(discovery->generationId);
			discovery.reset();
		}
	};

	SessionIpcServer::SessionIpcServer(SessionService& service, SessionIpcServerConfig config)
		: impl_(std::make_unique<Impl>(service, std::move(config))) {}
	SessionIpcServer::~SessionIpcServer() = default;
	ServiceResult<DiscoveryRecord> SessionIpcServer::start() { return impl_->start(); }
	void SessionIpcServer::stop() { impl_->stop(); }
	bool SessionIpcServer::isRunning() const noexcept { return impl_->running.load(); }
	std::optional<DiscoveryRecord> SessionIpcServer::discoveryRecord() const { return impl_->discovery; }

	struct SessionIpcClient::Impl {
		enum class SendResult {
			Sent,
			Invalid,
			Disconnected
		};

		struct PendingRequest {
			std::string operation;
			RequestContext context;
			std::string bodyJson;
			std::string sentGeneration;
			std::shared_ptr<std::promise<IpcResponse>> promise;
		};

		SessionIpcClientConfig config;
		UnixMillisProvider now;
		std::atomic<bool> running { false };
		std::atomic<bool> connected { false };
		std::thread worker;
		SocketHandle socket = INVALID_SOCKET_HANDLE;
		DiscoveryRecord discovery;
		FrameDecoder decoder;
		std::mutex mutex;
		std::condition_variable changed;
		std::map<std::string, PendingRequest> pending;
		SnapshotObserver snapshotObserver;
		ConnectionObserver connectionObserver;
		std::chrono::steady_clock::time_point nextConnect {};
		std::chrono::steady_clock::time_point nextHeartbeat {};

		explicit Impl(SessionIpcClientConfig value)
			: config(std::move(value)), now(config.nowUnixMillis ? config.nowUnixMillis : systemNowUnixMillis),
			decoder(config.maximumFrameBytes) {}

		~Impl() { stop(); }

		void notifyConnection(bool value) {
			if (connected.exchange(value) == value) return;
			ConnectionObserver observer;
			{
				std::lock_guard lock(mutex);
				observer = connectionObserver;
			}
			if (observer) observer(value);
		}

		void disconnect() {
			closeSocket(socket);
			socket = INVALID_SOCKET_HANDLE;
			decoder.reset();
			discovery = {};
			notifyConnection(false);
			nextConnect = std::chrono::steady_clock::now() + config.reconnectDelay;
		}

		bool connectToServer() {
			if (!config.discoveryFile || !socketRuntimeReady()) return false;
			auto record = config.discoveryFile->read(now(), config.maximumDiscoveryAge);
			if (!record) return false;
			auto candidate = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (candidate == INVALID_SOCKET_HANDLE) return false;
			sockaddr_in address {};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			address.sin_port = htons(record.value().port);
			if (::connect(candidate, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
				closeSocket(candidate);
				return false;
			}
			socket = candidate;
			discovery = record.value();
			nextHeartbeat = std::chrono::steady_clock::now();
			notifyConnection(true);
			return true;
		}

		SendResult sendEnvelope(PendingRequest& request) {
			Json body;
			try { body = Json::parse(request.bodyJson); }
			catch (std::exception const&) { return SendResult::Invalid; }
			Json envelope { { "kind", "request" }, { "protocolMajor", CURRENT_SESSION_PROTOCOL_MAJOR },
				{ "protocolMinor", CURRENT_SESSION_PROTOCOL_MINOR }, { "token", discovery.authenticationToken },
				{ "requestId", request.context.requestId }, { "operation", request.operation },
				{ "context", contextJson(request.context) }, { "body", std::move(body) } };
			auto framed = frameMessage(envelope.dump(), config.maximumFrameBytes);
			if (!framed) return SendResult::Invalid;
			if (!sendAll(socket, framed.value())) return SendResult::Disconnected;
			request.sentGeneration = discovery.generationId;
			return SendResult::Sent;
		}

		bool sendHeartbeat() {
			Json envelope { { "kind", "request" }, { "protocolMajor", CURRENT_SESSION_PROTOCOL_MAJOR },
				{ "protocolMinor", CURRENT_SESSION_PROTOCOL_MINOR }, { "token", discovery.authenticationToken },
				{ "operation", "heartbeat" } };
			std::mutex unused;
			return sendJson(socket, unused, envelope, config.maximumFrameBytes);
		}

		void expireDeadlines() {
			std::vector<std::shared_ptr<std::promise<IpcResponse>>> expired;
			std::vector<std::string> ids;
			{
				std::lock_guard lock(mutex);
				for (auto const& [requestId, request] : pending) {
					if (request.context.deadlineUnixMillis && *request.context.deadlineUnixMillis <= now()) {
						ids.push_back(requestId);
						expired.push_back(request.promise);
					}
				}
				for (auto const& id : ids) pending.erase(id);
			}
			for (std::size_t index = 0; index < ids.size(); ++index) {
				expired[index]->set_value({ ids[index], std::nullopt,
					transportError(ServiceErrorCode::DeadlineExceeded, "IPC request deadline elapsed") });
			}
		}

		bool sendPending() {
			std::vector<std::pair<std::string, PendingRequest>> unsent;
			{
				std::lock_guard lock(mutex);
				for (auto const& [requestId, request] : pending) {
					if (request.sentGeneration != discovery.generationId) unsent.emplace_back(requestId, request);
				}
			}
			for (auto& [requestId, request] : unsent) {
				auto const sent = sendEnvelope(request);
				if (sent == SendResult::Disconnected) return false;
				std::lock_guard lock(mutex);
				auto found = pending.find(requestId);
				if (found == pending.end()) continue;
				if (sent == SendResult::Invalid) {
					auto promise = found->second.promise;
					pending.erase(found);
					promise->set_value({ requestId, std::nullopt,
						transportError(ServiceErrorCode::InvalidRequest, "IPC request exceeds the frame limit") });
				} else {
					found->second.sentGeneration = request.sentGeneration;
				}
			}
			return true;
		}

		void processMessage(std::string const& payload) {
			try {
				auto const message = Json::parse(payload);
				if (message.value("protocolMajor", 0U) != CURRENT_SESSION_PROTOCOL_MAJOR) {
					disconnect();
					return;
				}
				auto const kind = message.value("kind", std::string {});
				if (kind == "heartbeat") return;
				if (kind == "event" && message.value("event", std::string {}) == "sessionSnapshot") {
					auto snapshot = readSnapshot(message.at("body"));
					SnapshotObserver observer;
					{
						std::lock_guard lock(mutex);
						observer = snapshotObserver;
					}
					if (observer) observer(snapshot);
					return;
				}
				if (kind != "response") throw std::runtime_error("Unexpected IPC message kind");
				auto const requestId = message.value("requestId", std::string {});
				std::shared_ptr<std::promise<IpcResponse>> promise;
				{
					std::lock_guard lock(mutex);
					auto found = pending.find(requestId);
					if (found == pending.end()) return;
					promise = found->second.promise;
					pending.erase(found);
				}
				IpcResponse response;
				response.requestId = requestId;
				if (message.value("ok", false)) response.payloadJson = message.at("body").dump();
				else response.error = readError(message.at("error"));
				promise->set_value(std::move(response));
			} catch (std::exception const&) {
				disconnect();
			}
		}

		bool receiveAvailable() {
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(socket, &readSet);
			timeval timeout {};
			timeout.tv_usec = 20'000;
#ifdef _WIN32
			auto const selected = select(0, &readSet, nullptr, nullptr, &timeout);
#else
			auto const selected = select(socket + 1, &readSet, nullptr, nullptr, &timeout);
#endif
			if (selected < 0) return false;
			if (selected == 0) return true;
			std::array<std::uint8_t, 16U * 1024U> received {};
#ifdef _WIN32
			auto const count = recv(socket, reinterpret_cast<char*>(received.data()), static_cast<int>(received.size()), 0);
#else
			auto const count = recv(socket, received.data(), received.size(), 0);
#endif
			if (count <= 0) return false;
			auto frames = decoder.append(std::span<std::uint8_t const>(received.data(), static_cast<std::size_t>(count)));
			if (!frames) return false;
			for (auto const& frame : frames.value()) processMessage(frame);
			return socket != INVALID_SOCKET_HANDLE;
		}

		void run() {
			nextConnect = std::chrono::steady_clock::now();
			while (running.load()) {
				expireDeadlines();
				if (socket == INVALID_SOCKET_HANDLE) {
					if (std::chrono::steady_clock::now() >= nextConnect && !connectToServer())
						nextConnect = std::chrono::steady_clock::now() + config.reconnectDelay;
					std::unique_lock lock(mutex);
					changed.wait_for(lock, std::chrono::milliseconds(20));
					continue;
				}
				if (!sendPending()) { disconnect(); continue; }
				if (std::chrono::steady_clock::now() >= nextHeartbeat) {
					if (!sendHeartbeat()) { disconnect(); continue; }
					nextHeartbeat = std::chrono::steady_clock::now() + config.heartbeatInterval;
				}
				if (!receiveAvailable()) disconnect();
			}
			disconnect();
			std::map<std::string, PendingRequest> abandoned;
			{
				std::lock_guard lock(mutex);
				abandoned.swap(pending);
			}
			for (auto& [requestId, request] : abandoned) request.promise->set_value({ requestId, std::nullopt,
				transportError(ServiceErrorCode::Unavailable, "IPC client stopped", true) });
		}

		void start() {
			if (running.exchange(true)) return;
			worker = std::thread([this] { run(); });
		}

		void stop() {
			if (!running.exchange(false)) return;
			closeSocket(socket);
			socket = INVALID_SOCKET_HANDLE;
			changed.notify_all();
			if (worker.joinable()) worker.join();
		}

		std::future<IpcResponse> request(std::string operation, RequestContext context, std::string bodyJson) {
			auto promise = std::make_shared<std::promise<IpcResponse>>();
			auto future = promise->get_future();
			auto const requestId = context.requestId;
		try {
			auto body = Json::parse(bodyJson);
			if (!body.is_object()) throw std::runtime_error("body");
			if (context.requestId.empty() || operation.empty()) throw std::runtime_error("identity");
		} catch (std::exception const&) {
			promise->set_value({ context.requestId, std::nullopt,
				transportError(ServiceErrorCode::InvalidRequest, "IPC request is invalid") });
			return future;
		}
		{
			std::lock_guard lock(mutex);
			if (pending.contains(requestId)) {
				promise->set_value({ requestId, std::nullopt,
					transportError(ServiceErrorCode::InvalidRequest, "Request ID is already pending") });
				return future;
			}
			if (pending.size() >= config.maximumPendingRequests) {
				promise->set_value({ requestId, std::nullopt,
					transportError(ServiceErrorCode::Unavailable, "IPC client queue is full", true) });
				return future;
			}
			pending.emplace(requestId, PendingRequest { std::move(operation), std::move(context),
				std::move(bodyJson), {}, promise });
		}
		changed.notify_one();
		return future;
		}

		void cancelPending(std::string const& requestId) {
			std::shared_ptr<std::promise<IpcResponse>> promise;
			{
				std::lock_guard lock(mutex);
				auto found = pending.find(requestId);
				if (found == pending.end()) return;
				promise = found->second.promise;
				pending.erase(found);
			}
			promise->set_value({ requestId, std::nullopt,
				transportError(ServiceErrorCode::TransferCancelled, "IPC request cancelled") });
		}
	};

	SessionIpcClient::SessionIpcClient(SessionIpcClientConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
	SessionIpcClient::~SessionIpcClient() = default;
	void SessionIpcClient::start() { impl_->start(); }
	void SessionIpcClient::stop() { impl_->stop(); }
	bool SessionIpcClient::isConnected() const noexcept { return impl_->connected.load(); }
	void SessionIpcClient::setSnapshotObserver(SnapshotObserver observer) {
		std::lock_guard lock(impl_->mutex);
		impl_->snapshotObserver = std::move(observer);
	}
	void SessionIpcClient::setConnectionObserver(ConnectionObserver observer) {
		std::lock_guard lock(impl_->mutex);
		impl_->connectionObserver = std::move(observer);
	}
	std::future<IpcResponse> SessionIpcClient::request(std::string operation, RequestContext context, std::string bodyJson) {
		return impl_->request(std::move(operation), std::move(context), std::move(bodyJson));
	}
	void SessionIpcClient::cancelPending(std::string const& requestId) { impl_->cancelPending(requestId); }

}
