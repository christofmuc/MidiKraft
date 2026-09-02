/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "SessionService.h"

#include <memory>

namespace midikraft::session {

	struct FakeTransferBehavior {
		std::optional<TransferState> failAt;
		ServiceError failure { ServiceErrorCode::AdaptationError, "Fake adaptation failure", false };
	};

	// Deterministic, in-memory implementation intended for service consumers and
	// protocol tests. Time and transfer progress advance only when explicitly
	// requested, so tests never need sleeps or operating-system scheduling.
	class FakeSessionService final : public SessionService {
	public:
		FakeSessionService();
		~FakeSessionService() override;

		FakeSessionService(FakeSessionService const&) = delete;
		FakeSessionService& operator=(FakeSessionService const&) = delete;

		[[nodiscard]] ServiceResult<ServiceResponse<ServerInfo>> getServerInfo(RequestContext const& request) override;
		[[nodiscard]] ServiceResult<ServiceResponse<PagedItems<SessionSynthInfo>>> listConfiguredSynthInstances(ListSynthsRequest const& request) override;
		[[nodiscard]] ServiceResult<ServiceResponse<SessionSynthInfo>> getConfiguredSynthInstance(GetSynthRequest const& request) override;
		[[nodiscard]] ServiceResult<ServiceResponse<PagedItems<PatchSummary>>> searchPatches(SearchPatchesRequest const& request) override;
		[[nodiscard]] ServiceResult<ServiceResponse<SessionPatch>> getPatch(GetPatchRequest const& request) override;
		[[nodiscard]] ServiceResult<ServiceResponse<TransferRecord>> applyToEditBuffer(ApplyToEditBufferRequest const& request) override;
		[[nodiscard]] ServiceResult<ServiceResponse<TransferRecord>> getTransferStatus(GetTransferStatusRequest const& request) override;
		[[nodiscard]] ServiceResult<ServiceResponse<TransferRecord>> cancelTransfer(CancelTransferRequest const& request) override;
		[[nodiscard]] ServiceResult<ServiceResponse<NavigationResult>> openKnobKraft(OpenKnobKraftRequest const& request) override;

		[[nodiscard]] ServiceResult<ServiceResponse<SessionSnapshot>> publishSession(PublishSessionRequest const& request) override;
		[[nodiscard]] ServiceResult<ServiceResponse<SessionSnapshot>> disconnectSession(DisconnectSessionRequest const& request) override;
		[[nodiscard]] SessionSnapshot currentSnapshot() const override;
		[[nodiscard]] ObserverId subscribe(SessionObserver observer, bool emitCurrent = true) override;
		void unsubscribe(ObserverId observerId) override;

		void setNowUnixMillis(std::int64_t nowUnixMillis);
		[[nodiscard]] std::int64_t nowUnixMillis() const;
		void advanceTransfers();
		void setTransferBehavior(std::string requestId, FakeTransferBehavior behavior);
		void setNavigationAvailable(bool available);

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};

}
