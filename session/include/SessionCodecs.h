/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "SessionTypes.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace midikraft::session {

	struct CodecLimits {
		std::size_t maxDocumentBytes = 16 * 1024 * 1024;
		std::size_t maxPayloadBytes = 8 * 1024 * 1024;
		std::size_t maxStringBytes = 64 * 1024;
		std::size_t maxSounds = 256;
	};

	enum class CodecErrorCode {
		DocumentTooLarge,
		InvalidJson,
		MissingField,
		InvalidFieldType,
		InvalidValue,
		UnsupportedVersion,
		InvalidBase64,
		PayloadTooLarge,
		FingerprintMismatch
	};

	struct CodecError {
		CodecErrorCode code;
		std::string path;
		std::string message;
	};

	template<typename T>
	class CodecResult {
	public:
		static CodecResult success(T value) {
			return CodecResult(std::move(value), std::nullopt);
		}

		static CodecResult failure(CodecError error) {
			return CodecResult(std::nullopt, std::move(error));
		}

		[[nodiscard]] bool hasValue() const noexcept { return value_.has_value(); }
		[[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }
		[[nodiscard]] T const& value() const& { return value_.value(); }
		[[nodiscard]] T&& value() && { return std::move(value_).value(); }
		[[nodiscard]] CodecError const& error() const { return error_.value(); }

	private:
		CodecResult(std::optional<T> value, std::optional<CodecError> error)
			: value_(std::move(value)), error_(std::move(error)) {}

		std::optional<T> value_;
		std::optional<CodecError> error_;
	};

	class SessionPatchCodec {
	public:
		[[nodiscard]] static CodecResult<std::string> encode(SessionPatch const& patch, CodecLimits const& limits = {}) noexcept;
		[[nodiscard]] static CodecResult<SessionPatch> decode(std::string_view json, CodecLimits const& limits = {}) noexcept;
		[[nodiscard]] static CodecResult<std::string> fingerprint(SessionPatch const& patch, CodecLimits const& limits = {}) noexcept;
	};

	class SessionManifestCodec {
	public:
		[[nodiscard]] static CodecResult<std::string> encode(SessionManifest const& manifest, CodecLimits const& limits = {}) noexcept;
		[[nodiscard]] static CodecResult<SessionManifest> decode(std::string_view json, CodecLimits const& limits = {}) noexcept;
	};

}
