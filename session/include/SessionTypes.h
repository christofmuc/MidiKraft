/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace midikraft::session {

	inline constexpr std::uint32_t CURRENT_PATCH_FORMAT_VERSION = 1;
	inline constexpr std::uint32_t CURRENT_MANIFEST_SCHEMA_VERSION = 1;

	struct PatchProvenance {
		std::optional<std::string> databaseId;
		std::optional<std::int32_t> bank;
		std::optional<std::int32_t> program;

		bool operator==(PatchProvenance const& other) const = default;
	};

	// The v1 fingerprint is SHA-256 over the ASCII domain
	// "KnobKraft.SessionPatchFingerprint.v1" plus its NUL terminator, followed
	// by adaptationId, dataTypeId, and decoded payload bytes. Each value is
	// prefixed by its unsigned 64-bit big-endian byte length. The result is
	// lowercase hex prefixed with "sha256:". Patch name and provenance are
	// deliberately excluded because they do not affect interpretation.
	struct SessionPatch {
		std::uint32_t formatVersion = CURRENT_PATCH_FORMAT_VERSION;
		std::string adaptationId;
		std::string dataTypeId;
		std::string name;
		std::string fingerprint;
		std::vector<std::uint8_t> payload;
		std::optional<PatchProvenance> source;

		bool operator==(SessionPatch const& other) const = default;
	};

	struct SynthBinding {
		std::optional<std::string> configuredSynthInstanceId;
		std::optional<std::string> fallbackAdaptationId;

		bool operator==(SynthBinding const& other) const = default;
	};

	enum class RecallPolicy {
		Manual,
		Ask,
		AutomaticWhenStopped
	};

	struct SessionSound {
		std::string soundId;
		SessionPatch patch;

		bool operator==(SessionSound const& other) const = default;
	};

	struct SessionManifest {
		std::uint32_t schemaVersion = CURRENT_MANIFEST_SCHEMA_VERSION;
		std::string pluginInstanceId;
		std::string instanceName;
		SynthBinding binding;
		RecallPolicy recallPolicy = RecallPolicy::Manual;
		std::optional<std::string> selectedSoundId;
		std::vector<SessionSound> sounds;

		bool operator==(SessionManifest const& other) const = default;
	};

	enum class TransferState {
		Accepted,
		Queued,
		Preparing,
		Sending,
		Verifying,
		Succeeded,
		Failed,
		Cancelled
	};

	enum class VerificationState {
		NotAttempted,
		Unverified,
		Verified,
		Mismatch
	};

	struct TransferStatus {
		std::string transferId;
		std::string requestId;
		TransferState state = TransferState::Accepted;
		VerificationState verification = VerificationState::NotAttempted;
		std::optional<double> progress;
		std::string detail;

		bool operator==(TransferStatus const& other) const = default;
	};

}
