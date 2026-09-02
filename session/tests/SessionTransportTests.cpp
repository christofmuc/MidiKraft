#include "FakeSessionService.h"
#include "SessionCodecs.h"
#include "SessionTransport.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace {
	using namespace midikraft::session;
	using namespace std::chrono_literals;
	using Json = nlohmann::json;

	int failures = 0;

	void check(bool condition, char const* expression, int line) {
		if (!condition) {
			std::cerr << "line " << line << ": check failed: " << expression << '\n';
			++failures;
		}
	}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

	std::filesystem::path uniqueTestDirectory(std::string const& name) {
		auto const value = std::chrono::steady_clock::now().time_since_epoch().count();
		auto path = std::filesystem::temp_directory_path() / ("knobkraft-ipc-test-" + name + "-" + std::to_string(value));
		std::filesystem::create_directories(path);
		return path;
	}

	RequestContext context(std::string id, std::string client = "client-a", std::string plugin = "plugin-a",
		std::optional<std::int64_t> deadline = std::nullopt) {
		return { std::move(id), std::move(client), std::move(plugin), deadline };
	}

	template<typename Predicate>
	bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout = 3s) {
		auto const end = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < end) {
			if (predicate()) return true;
			std::this_thread::sleep_for(10ms);
		}
		return predicate();
	}

	IpcResponse wait(std::future<IpcResponse>& future) {
		CHECK(future.wait_for(3s) == std::future_status::ready);
		if (future.wait_for(0ms) != std::future_status::ready)
			return { {}, std::nullopt, ServiceError { ServiceErrorCode::InternalError, "test timeout", false } };
		return future.get();
	}

	SessionIpcServerConfig serverConfig(std::shared_ptr<DiscoveryFile> discovery,
		std::function<std::int64_t()> now = {}) {
		static auto counter = std::make_shared<std::atomic<int>>(0);
		SessionIpcServerConfig result;
		result.discoveryFile = std::move(discovery);
		result.nowUnixMillis = std::move(now);
		result.tokenGenerator = [] {
			return "0123456789abcdef-test-value-" + std::to_string(counter->fetch_add(1));
		};
		result.staleClientTimeout = 500ms;
		return result;
	}

	SessionIpcClientConfig clientConfig(std::shared_ptr<DiscoveryFile> discovery,
		std::function<std::int64_t()> now = {}) {
		SessionIpcClientConfig result;
		result.discoveryFile = std::move(discovery);
		result.nowUnixMillis = std::move(now);
		result.maximumDiscoveryAge = 10s;
		result.reconnectDelay = 20ms;
		result.heartbeatInterval = 50ms;
		return result;
	}

	void framingRejectsMalformedAndOversizedInput() {
		auto first = frameMessage("one");
		auto second = frameMessage("two");
		CHECK(first && second);
		std::vector<std::uint8_t> joined = first.value();
		joined.insert(joined.end(), second.value().begin(), second.value().end());
		FrameDecoder decoder(16);
		auto partial = decoder.append(std::span<std::uint8_t const>(joined.data(), 2));
		CHECK(partial && partial.value().empty());
		auto rest = decoder.append(std::span<std::uint8_t const>(joined.data() + 2, joined.size() - 2));
		CHECK(rest && rest.value() == std::vector<std::string>({ "one", "two" }));
		auto largeFirst = frameMessage(std::string(16, 'a'), 16);
		auto largeSecond = frameMessage(std::string(16, 'b'), 16);
		CHECK(largeFirst && largeSecond);
		std::vector<std::uint8_t> largeJoined = largeFirst.value();
		largeJoined.insert(largeJoined.end(), largeSecond.value().begin(), largeSecond.value().end());
		auto bothLarge = decoder.append(largeJoined);
		CHECK(bothLarge && bothLarge.value() == std::vector<std::string>({ std::string(16, 'a'), std::string(16, 'b') }));

		std::array<std::uint8_t, 4> oversized { 0, 0, 1, 0 };
		auto rejected = decoder.append(oversized);
		CHECK(!rejected);
		CHECK(!frameMessage(std::string(17, 'x'), 16));
		std::array<std::uint8_t, 4> empty { 0, 0, 0, 0 };
		CHECK(!decoder.append(empty));
	}

	void discoveryIsBoundedVersionedAndStaleAware() {
		auto directory = uniqueTestDirectory("discovery");
		auto file = std::make_shared<DiscoveryFile>(directory / "bridge.json");
		DiscoveryRecord record { 1, 0, 1234, 42, "generation-0123456789", "token-0123456789abcdef", 1000 };
		CHECK(file->write(record));
		auto read = file->read(1050, 100ms);
		CHECK(read && read.value() == record);
		CHECK(!file->read(1200, 100ms));
		record.protocolMajor = 99;
		CHECK(file->write(record));
		auto incompatible = file->read(1000, 100ms);
		CHECK(!incompatible);
		if (!incompatible) CHECK(incompatible.error().code == ServiceErrorCode::ProtocolIncompatible);
		file->removeIfGenerationMatches("different");
		CHECK(std::filesystem::exists(file->path()));
		file->removeIfGenerationMatches(record.generationId);
		CHECK(!std::filesystem::exists(file->path()));
		std::filesystem::remove_all(directory);
	}

	void basicRequestsProgressAndCancellationCrossTheTransport() {
		auto directory = uniqueTestDirectory("basic");
		auto discovery = std::make_shared<DiscoveryFile>(directory / "bridge.json");
		FakeSessionService service;
		SessionIpcServer server(service, serverConfig(discovery));
		CHECK(server.start());
		SessionIpcClient client(clientConfig(discovery));
		std::atomic<int> snapshots = 0;
		client.setSnapshotObserver([&](SessionSnapshot const&) { ++snapshots; });
		client.start();
		CHECK(waitUntil([&] { return client.isConnected(); }));

		auto infoFuture = client.request("getServerInfo", context("info"));
		auto info = wait(infoFuture);
		CHECK(info.hasValue());
		if (info.payloadJson) CHECK(Json::parse(*info.payloadJson).at("productName") == "KnobKraft Fake Session Service");

		auto synthsFuture = client.request("listConfiguredSynthInstances", context("synths"), R"({"pageSize":2})");
		auto synths = wait(synthsFuture);
		CHECK(synths.hasValue());
		if (synths.payloadJson) CHECK(Json::parse(*synths.payloadJson).at("items").size() == 2);
		auto malformedFuture = client.request("listConfiguredSynthInstances", context("malformed"), R"({"pageSize":"many"})");
		auto malformed = wait(malformedFuture);
		CHECK(malformed.error.has_value());
		if (malformed.error) CHECK(malformed.error->code == ServiceErrorCode::InvalidRequest);

		auto patchFuture = client.request("getPatch", context("patch"), R"({"patchId":"patch-warm-bass"})");
		auto patch = wait(patchFuture);
		CHECK(patch.hasValue());
		Json applyBody { { "configuredSynthInstanceId", "synth-matrix" },
			{ "expectedAdaptationId", "Oberheim Matrix 1000" }, { "patch", Json::parse(*patch.payloadJson) } };
		auto applyFuture = client.request("applyToEditBuffer", context("send"), applyBody.dump());
		auto applied = wait(applyFuture);
		CHECK(applied.hasValue());
		auto transferId = Json::parse(*applied.payloadJson).at("status").at("transferId").get<std::string>();
		CHECK(waitUntil([&] { return snapshots.load() > 0; }));

		auto cancelFuture = client.request("cancelTransfer", context("cancel"),
			Json { { "transferId", transferId } }.dump());
		auto cancelled = wait(cancelFuture);
		CHECK(cancelled.hasValue());
		if (cancelled.payloadJson) CHECK(Json::parse(*cancelled.payloadJson).at("status").at("state")
			== static_cast<int>(TransferState::Cancelled));
		client.stop();
		server.stop();
		std::filesystem::remove_all(directory);
	}

	void badTokensAndDeadlinesFailSafely() {
		auto directory = uniqueTestDirectory("security");
		auto realDiscovery = std::make_shared<DiscoveryFile>(directory / "real.json");
		auto badDiscovery = std::make_shared<DiscoveryFile>(directory / "bad.json");
		FakeSessionService service;
		SessionIpcServer server(service, serverConfig(realDiscovery));
		auto started = server.start();
		CHECK(started);
		auto bad = started.value();
		bad.authenticationToken = "bad-token-0123456789abcdef";
		CHECK(badDiscovery->write(bad));
		SessionIpcClient client(clientConfig(badDiscovery));
		client.start();
		auto unauthorizedFuture = client.request("getServerInfo", context("unauthorized"));
		auto unauthorized = wait(unauthorizedFuture);
		CHECK(unauthorized.error.has_value());
		if (unauthorized.error) CHECK(unauthorized.error->code == ServiceErrorCode::AuthenticationFailed);
		client.stop();

		auto tinySettings = clientConfig(realDiscovery);
		tinySettings.maximumFrameBytes = 256;
		SessionIpcClient tinyClient(tinySettings);
		tinyClient.start();
		CHECK(waitUntil([&] { return tinyClient.isConnected(); }));
		auto oversizedFuture = tinyClient.request("getServerInfo", context("oversized"),
			Json { { "padding", std::string(512, 'x') } }.dump());
		auto oversized = wait(oversizedFuture);
		CHECK(oversized.error.has_value());
		if (oversized.error) CHECK(oversized.error->code == ServiceErrorCode::InvalidRequest);
		tinyClient.stop();

		auto missingDiscovery = std::make_shared<DiscoveryFile>(directory / "missing.json");
		std::atomic<std::int64_t> clock = 2000;
		SessionIpcClient unavailable(clientConfig(missingDiscovery, [&] { return clock.load(); }));
		unavailable.start();
		auto timeoutFuture = unavailable.request("getServerInfo", context("timeout", "client", "plugin", 2001));
		clock.store(2001);
		auto timeout = wait(timeoutFuture);
		CHECK(timeout.error.has_value());
		if (timeout.error) CHECK(timeout.error->code == ServiceErrorCode::DeadlineExceeded);
		unavailable.stop();

		std::atomic<std::int64_t> serverClock = 3000;
		std::atomic<std::int64_t> clientClock = 3000;
		auto deadlineDiscovery = std::make_shared<DiscoveryFile>(directory / "deadline.json");
		SessionIpcServer deadlineServer(service, serverConfig(deadlineDiscovery, [&] { return serverClock.load(); }));
		CHECK(deadlineServer.start());
		SessionIpcClient deadlineClient(clientConfig(deadlineDiscovery, [&] { return clientClock.load(); }));
		deadlineClient.start();
		CHECK(waitUntil([&] { return deadlineClient.isConnected(); }));
		serverClock.store(3001);
		auto expiredFuture = deadlineClient.request("getServerInfo", context("server-expired", "client", "plugin", 3001));
		auto expired = wait(expiredFuture);
		CHECK(expired.error.has_value());
		if (expired.error) CHECK(expired.error->code == ServiceErrorCode::DeadlineExceeded);
		deadlineClient.stop();
		deadlineServer.stop();
		server.stop();
		std::filesystem::remove_all(directory);
	}

	void multipleClientsAndIdempotentRetryWork() {
		auto directory = uniqueTestDirectory("multi");
		auto discovery = std::make_shared<DiscoveryFile>(directory / "bridge.json");
		FakeSessionService service;
		SessionIpcServer server(service, serverConfig(discovery));
		CHECK(server.start());
		SessionIpcClient first(clientConfig(discovery));
		SessionIpcClient second(clientConfig(discovery));
		first.start();
		second.start();
		CHECK(waitUntil([&] { return first.isConnected() && second.isConnected(); }));
		auto oneFuture = first.request("publishSession", context("publish-1", "client-1", "plugin-1"),
			R"({"instanceName":"Bass","binding":{"configuredSynthInstanceId":"synth-matrix"}})");
		auto twoFuture = second.request("publishSession", context("publish-2", "client-2", "plugin-2"),
			R"({"instanceName":"Pad","binding":{"configuredSynthInstanceId":"synth-prophet"}})");
		CHECK(wait(oneFuture).hasValue());
		CHECK(wait(twoFuture).hasValue());

		auto patchFuture = first.request("getPatch", context("get-idempotent"), R"({"patchId":"patch-warm-bass"})");
		auto patch = wait(patchFuture);
		Json body { { "configuredSynthInstanceId", "synth-matrix" },
			{ "expectedAdaptationId", "Oberheim Matrix 1000" }, { "patch", Json::parse(*patch.payloadJson) } };
		auto originalFuture = first.request("applyToEditBuffer", context("stable-mutation", "client-1", "plugin-1"), body.dump());
		auto original = wait(originalFuture);
		server.stop();
		CHECK(waitUntil([&] { return !first.isConnected() && !second.isConnected(); }));
		SessionIpcServer restartedServer(service, serverConfig(discovery));
		CHECK(restartedServer.start());
		CHECK(waitUntil([&] { return first.isConnected() && second.isConnected(); }));
		auto retryFuture = first.request("applyToEditBuffer", context("stable-mutation", "client-1", "plugin-1"), body.dump());
		auto retry = wait(retryFuture);
		CHECK(original.hasValue() && retry.hasValue());
		if (original.payloadJson && retry.payloadJson) CHECK(Json::parse(*original.payloadJson).at("status").at("transferId")
			== Json::parse(*retry.payloadJson).at("status").at("transferId"));
		first.stop();
		second.stop();
		restartedServer.stop();
		std::filesystem::remove_all(directory);
	}

	void heartbeatsKeepSessionsAliveAndDisconnectRemovesThem() {
		auto directory = uniqueTestDirectory("heartbeat");
		auto discovery = std::make_shared<DiscoveryFile>(directory / "bridge.json");
		FakeSessionService service;
		auto serverSettings = serverConfig(discovery);
		serverSettings.staleClientTimeout = 200ms;
		SessionIpcServer server(service, serverSettings);
		CHECK(server.start());
		SessionIpcClient observer(clientConfig(discovery));
		SessionIpcClient publisher(clientConfig(discovery));
		std::atomic<std::size_t> sessionCount = 0;
		observer.setSnapshotObserver([&](SessionSnapshot const& snapshot) { sessionCount.store(snapshot.sessions.size()); });
		observer.start();
		publisher.start();
		CHECK(waitUntil([&] { return observer.isConnected() && publisher.isConnected(); }));
		auto publishedFuture = publisher.request("publishSession", context("heartbeat-publish", "heartbeat-client", "heartbeat-plugin"),
			R"({"instanceName":"Heartbeat","binding":{"configuredSynthInstanceId":"synth-matrix"}})");
		CHECK(wait(publishedFuture).hasValue());
		CHECK(waitUntil([&] { return sessionCount.load() == 1; }));
		std::this_thread::sleep_for(400ms);
		CHECK(publisher.isConnected());
		CHECK(sessionCount.load() == 1);
		publisher.stop();
		CHECK(waitUntil([&] { return sessionCount.load() == 0; }));
		observer.stop();
		server.stop();
		std::filesystem::remove_all(directory);
	}

	void clientReconnectsAfterServerRestart() {
		auto directory = uniqueTestDirectory("restart");
		auto discovery = std::make_shared<DiscoveryFile>(directory / "bridge.json");
		FakeSessionService firstService;
		SessionIpcServer firstServer(firstService, serverConfig(discovery));
		CHECK(firstServer.start());
		SessionIpcClient client(clientConfig(discovery));
		client.start();
		CHECK(waitUntil([&] { return client.isConnected(); }));
		firstServer.stop();
		CHECK(waitUntil([&] { return !client.isConnected(); }));

		auto pending = client.request("getServerInfo", context("after-restart"));
		FakeSessionService secondService;
		SessionIpcServer secondServer(secondService, serverConfig(discovery));
		CHECK(secondServer.start());
		auto response = wait(pending);
		CHECK(response.hasValue());
		CHECK(waitUntil([&] { return client.isConnected(); }));
		client.stop();
		secondServer.stop();
		std::filesystem::remove_all(directory);
	}
}

int main() {
	framingRejectsMalformedAndOversizedInput();
	discoveryIsBoundedVersionedAndStaleAware();
	basicRequestsProgressAndCancellationCrossTheTransport();
	badTokensAndDeadlinesFailSafely();
	multipleClientsAndIdempotentRetryWork();
	heartbeatsKeepSessionsAliveAndDisconnectRemovesThem();
	clientReconnectsAfterServerRestart();
	if (failures != 0) std::cerr << failures << " session transport test(s) failed\n";
	return failures == 0 ? 0 : 1;
}
