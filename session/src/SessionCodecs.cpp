/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SessionCodecs.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace midikraft::session {
	namespace {
		using Json = nlohmann::json;

		struct DecodeFailure {
			CodecError error;
		};

		[[noreturn]] void fail(CodecErrorCode code, std::string path, std::string message) {
			throw DecodeFailure { CodecError { code, std::move(path), std::move(message) } };
		}

		void checkString(std::string const& value, std::string const& path, CodecLimits const& limits, bool allowEmpty = true) {
			if (value.size() > limits.maxStringBytes) {
				fail(CodecErrorCode::InvalidValue, path, "string exceeds configured size limit");
			}
			if (!allowEmpty && value.empty()) {
				fail(CodecErrorCode::InvalidValue, path, "string must not be empty");
			}
		}

		std::string requiredString(Json const& object, char const* key, std::string const& path, CodecLimits const& limits, bool allowEmpty = true) {
			auto const item = object.find(key);
			if (item == object.end()) {
				fail(CodecErrorCode::MissingField, path + "." + key, "required field is missing");
			}
			if (!item->is_string()) {
				fail(CodecErrorCode::InvalidFieldType, path + "." + key, "expected a string");
			}
			auto value = item->get<std::string>();
			checkString(value, path + "." + key, limits, allowEmpty);
			return value;
		}

		std::optional<std::string> optionalString(Json const& object, char const* key, std::string const& path, CodecLimits const& limits, bool allowEmpty = true) {
			auto const item = object.find(key);
			if (item == object.end() || item->is_null()) {
				return std::nullopt;
			}
			if (!item->is_string()) {
				fail(CodecErrorCode::InvalidFieldType, path + "." + key, "expected a string or null");
			}
			auto value = item->get<std::string>();
			checkString(value, path + "." + key, limits, allowEmpty);
			return value;
		}

		std::string requiredPayloadString(Json const& object, std::string const& path, CodecLimits const& limits) {
			auto const item = object.find("payload");
			if (item == object.end()) {
				fail(CodecErrorCode::MissingField, path + ".payload", "required field is missing");
			}
			if (!item->is_string()) {
				fail(CodecErrorCode::InvalidFieldType, path + ".payload", "expected a string");
			}
			auto value = item->get<std::string>();
			if (value.size() > limits.maxDocumentBytes) {
				fail(CodecErrorCode::DocumentTooLarge, path + ".payload", "encoded payload exceeds the document size limit");
			}
			return value;
		}

		std::uint32_t requiredVersion(Json const& object, char const* key, std::uint32_t supported, std::string const& path) {
			auto const item = object.find(key);
			if (item == object.end()) {
				fail(CodecErrorCode::MissingField, path + "." + key, "required version field is missing");
			}
			if (!item->is_number_unsigned()) {
				fail(CodecErrorCode::InvalidFieldType, path + "." + key, "expected an unsigned integer");
			}
			auto const value = item->get<std::uint64_t>();
			if (value != supported) {
				fail(CodecErrorCode::UnsupportedVersion, path + "." + key, "only version " + std::to_string(supported) + " is supported");
			}
			return static_cast<std::uint32_t>(value);
		}

		std::optional<std::int32_t> optionalInt32(Json const& object, char const* key, std::string const& path) {
			auto const item = object.find(key);
			if (item == object.end() || item->is_null()) {
				return std::nullopt;
			}
			if (!item->is_number_integer()) {
				fail(CodecErrorCode::InvalidFieldType, path + "." + key, "expected an integer or null");
			}
			auto const value = item->get<std::int64_t>();
			if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
				fail(CodecErrorCode::InvalidValue, path + "." + key, "integer is outside the supported range");
			}
			return static_cast<std::int32_t>(value);
		}

		// Match Patch Interchange Format: standard padded RFC 4648 Base64, with
		// no whitespace or URL-safe alphabet substitutions.
		constexpr char BASE64_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		std::string encodeBase64(std::vector<std::uint8_t> const& data) {
			std::string result;
			result.reserve(((data.size() + 2) / 3) * 4);
			for (std::size_t offset = 0; offset < data.size(); offset += 3) {
				auto const remaining = data.size() - offset;
				auto const value = (static_cast<std::uint32_t>(data[offset]) << 16)
					| (remaining > 1 ? static_cast<std::uint32_t>(data[offset + 1]) << 8 : 0)
					| (remaining > 2 ? static_cast<std::uint32_t>(data[offset + 2]) : 0);
				result.push_back(BASE64_ALPHABET[(value >> 18) & 0x3f]);
				result.push_back(BASE64_ALPHABET[(value >> 12) & 0x3f]);
				result.push_back(remaining > 1 ? BASE64_ALPHABET[(value >> 6) & 0x3f] : '=');
				result.push_back(remaining > 2 ? BASE64_ALPHABET[value & 0x3f] : '=');
			}
			return result;
		}

		int base64Value(char character) {
			if (character >= 'A' && character <= 'Z') return character - 'A';
			if (character >= 'a' && character <= 'z') return character - 'a' + 26;
			if (character >= '0' && character <= '9') return character - '0' + 52;
			if (character == '+') return 62;
			if (character == '/') return 63;
			return -1;
		}

		std::vector<std::uint8_t> decodeBase64(std::string const& encoded, std::string const& path, CodecLimits const& limits) {
			if (encoded.size() % 4 != 0) {
				fail(CodecErrorCode::InvalidBase64, path, "base64 length must be a multiple of four");
			}
			auto const padding = encoded.empty() ? 0U : (encoded.back() == '=' ? (encoded.size() > 1 && encoded[encoded.size() - 2] == '=' ? 2U : 1U) : 0U);
			auto const decodedSize = encoded.size() / 4 * 3 - padding;
			if (decodedSize > limits.maxPayloadBytes) {
				fail(CodecErrorCode::PayloadTooLarge, path, "decoded payload exceeds configured size limit");
			}

			std::vector<std::uint8_t> result;
			result.reserve(decodedSize);
			for (std::size_t offset = 0; offset < encoded.size(); offset += 4) {
				auto const finalBlock = offset + 4 == encoded.size();
				auto const a = base64Value(encoded[offset]);
				auto const b = base64Value(encoded[offset + 1]);
				auto const c = encoded[offset + 2] == '=' ? -2 : base64Value(encoded[offset + 2]);
				auto const d = encoded[offset + 3] == '=' ? -2 : base64Value(encoded[offset + 3]);
				if (a < 0 || b < 0 || c == -1 || d == -1 || (c == -2 && d != -2) || (!finalBlock && (c == -2 || d == -2))) {
					fail(CodecErrorCode::InvalidBase64, path, "payload is not canonical base64");
				}
				if ((c == -2 && (b & 0x0f) != 0) || (d == -2 && c >= 0 && (c & 0x03) != 0)) {
					fail(CodecErrorCode::InvalidBase64, path, "payload contains non-zero padding bits");
				}
				auto const value = (static_cast<std::uint32_t>(a) << 18)
					| (static_cast<std::uint32_t>(b) << 12)
					| (c >= 0 ? static_cast<std::uint32_t>(c) << 6 : 0)
					| (d >= 0 ? static_cast<std::uint32_t>(d) : 0);
				result.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
				if (c >= 0) result.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
				if (d >= 0) result.push_back(static_cast<std::uint8_t>(value & 0xff));
			}
			return result;
		}

		constexpr std::array<std::uint32_t, 64> SHA256_CONSTANTS {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
		};

		std::uint32_t rotateRight(std::uint32_t value, unsigned amount) {
			return value >> amount | value << (32 - amount);
		}

		std::array<std::uint8_t, 32> sha256(std::vector<std::uint8_t> bytes) {
			auto const bitLength = static_cast<std::uint64_t>(bytes.size()) * 8;
			bytes.push_back(0x80);
			while (bytes.size() % 64 != 56) bytes.push_back(0);
			for (int shift = 56; shift >= 0; shift -= 8) bytes.push_back(static_cast<std::uint8_t>(bitLength >> shift));

			std::array<std::uint32_t, 8> hash {
				0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
				0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
			};
			for (std::size_t chunk = 0; chunk < bytes.size(); chunk += 64) {
				std::array<std::uint32_t, 64> words {};
				for (std::size_t index = 0; index < 16; ++index) {
					auto const offset = chunk + index * 4;
					words[index] = static_cast<std::uint32_t>(bytes[offset]) << 24
						| static_cast<std::uint32_t>(bytes[offset + 1]) << 16
						| static_cast<std::uint32_t>(bytes[offset + 2]) << 8
						| static_cast<std::uint32_t>(bytes[offset + 3]);
				}
				for (std::size_t index = 16; index < words.size(); ++index) {
					auto const s0 = rotateRight(words[index - 15], 7) ^ rotateRight(words[index - 15], 18) ^ (words[index - 15] >> 3);
					auto const s1 = rotateRight(words[index - 2], 17) ^ rotateRight(words[index - 2], 19) ^ (words[index - 2] >> 10);
					words[index] = words[index - 16] + s0 + words[index - 7] + s1;
				}

				auto a = hash[0]; auto b = hash[1]; auto c = hash[2]; auto d = hash[3];
				auto e = hash[4]; auto f = hash[5]; auto g = hash[6]; auto h = hash[7];
				for (std::size_t index = 0; index < words.size(); ++index) {
					auto const sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
					auto const choice = (e & f) ^ (~e & g);
					auto const temporary1 = h + sum1 + choice + SHA256_CONSTANTS[index] + words[index];
					auto const sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
					auto const majority = (a & b) ^ (a & c) ^ (b & c);
					auto const temporary2 = sum0 + majority;
					h = g; g = f; f = e; e = d + temporary1; d = c; c = b; b = a; a = temporary1 + temporary2;
				}
				hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d;
				hash[4] += e; hash[5] += f; hash[6] += g; hash[7] += h;
			}

			std::array<std::uint8_t, 32> digest {};
			for (std::size_t index = 0; index < hash.size(); ++index) {
				for (std::size_t byte = 0; byte < 4; ++byte) {
					digest[index * 4 + byte] = static_cast<std::uint8_t>(hash[index] >> (24 - byte * 8));
				}
			}
			return digest;
		}

		void appendUint64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
			for (int shift = 56; shift >= 0; shift -= 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
		}

		void appendString(std::vector<std::uint8_t>& bytes, std::string const& value) {
			appendUint64(bytes, value.size());
			bytes.insert(bytes.end(), value.begin(), value.end());
		}

		std::string calculateFingerprint(SessionPatch const& patch, CodecLimits const& limits) {
			checkString(patch.adaptationId, "$.adaptationId", limits, false);
			checkString(patch.dataTypeId, "$.dataTypeId", limits, false);
			if (patch.payload.size() > limits.maxPayloadBytes) {
				fail(CodecErrorCode::PayloadTooLarge, "$.payload", "payload exceeds configured size limit");
			}
			static constexpr char DOMAIN[] = "KnobKraft.SessionPatchFingerprint.v1";
			std::vector<std::uint8_t> input(DOMAIN, DOMAIN + sizeof(DOMAIN));
			appendString(input, patch.adaptationId);
			appendString(input, patch.dataTypeId);
			appendUint64(input, patch.payload.size());
			input.insert(input.end(), patch.payload.begin(), patch.payload.end());
			auto const digest = sha256(std::move(input));
			static constexpr char HEX[] = "0123456789abcdef";
			std::string result = "sha256:";
			result.reserve(7 + digest.size() * 2);
			for (auto byte : digest) {
				result.push_back(HEX[byte >> 4]);
				result.push_back(HEX[byte & 0x0f]);
			}
			return result;
		}

		Json patchToJson(SessionPatch const& patch, CodecLimits const& limits) {
			if (patch.formatVersion != CURRENT_PATCH_FORMAT_VERSION) {
				fail(CodecErrorCode::UnsupportedVersion, "$.formatVersion", "only version 1 can be encoded");
			}
			checkString(patch.name, "$.name", limits);
			if (patch.payload.size() > limits.maxPayloadBytes) {
				fail(CodecErrorCode::PayloadTooLarge, "$.payload", "payload exceeds configured size limit");
			}
			auto const encodedPayloadSize = ((patch.payload.size() + 2) / 3) * 4;
			if (encodedPayloadSize > limits.maxDocumentBytes) {
				fail(CodecErrorCode::DocumentTooLarge, "$.payload", "encoded payload exceeds the document size limit");
			}
			auto const fingerprint = calculateFingerprint(patch, limits);
			if (!patch.fingerprint.empty() && patch.fingerprint != fingerprint) {
				fail(CodecErrorCode::FingerprintMismatch, "$.fingerprint", "stored fingerprint does not match the patch data");
			}
			Json object {
				{ "formatVersion", patch.formatVersion },
				{ "adaptationId", patch.adaptationId },
				{ "dataTypeId", patch.dataTypeId },
				{ "name", patch.name },
				{ "fingerprint", fingerprint },
				{ "payloadEncoding", "base64" },
				{ "payload", encodeBase64(patch.payload) }
			};
			if (patch.source) {
				Json source = Json::object();
				if (patch.source->databaseId) {
					checkString(*patch.source->databaseId, "$.source.databaseId", limits, false);
					source["databaseId"] = *patch.source->databaseId;
				}
				if (patch.source->bank) source["bank"] = *patch.source->bank;
				if (patch.source->program) source["program"] = *patch.source->program;
				object["source"] = std::move(source);
			}
			return object;
		}

		SessionPatch patchFromJson(Json const& object, std::string const& path, CodecLimits const& limits) {
			if (!object.is_object()) fail(CodecErrorCode::InvalidFieldType, path, "expected an object");
			SessionPatch patch;
			patch.formatVersion = requiredVersion(object, "formatVersion", CURRENT_PATCH_FORMAT_VERSION, path);
			patch.adaptationId = requiredString(object, "adaptationId", path, limits, false);
			patch.dataTypeId = requiredString(object, "dataTypeId", path, limits, false);
			patch.name = requiredString(object, "name", path, limits);
			patch.fingerprint = requiredString(object, "fingerprint", path, limits, false);
			auto const encoding = requiredString(object, "payloadEncoding", path, limits, false);
			if (encoding != "base64") fail(CodecErrorCode::InvalidValue, path + ".payloadEncoding", "only base64 payloads are supported");
			patch.payload = decodeBase64(requiredPayloadString(object, path, limits), path + ".payload", limits);
			auto const source = object.find("source");
			if (source != object.end() && !source->is_null()) {
				if (!source->is_object()) fail(CodecErrorCode::InvalidFieldType, path + ".source", "expected an object or null");
				patch.source = PatchProvenance {
					optionalString(*source, "databaseId", path + ".source", limits, false),
					optionalInt32(*source, "bank", path + ".source"),
					optionalInt32(*source, "program", path + ".source")
				};
			}
			auto const expected = calculateFingerprint(patch, limits);
			if (patch.fingerprint != expected) {
				fail(CodecErrorCode::FingerprintMismatch, path + ".fingerprint", "fingerprint does not match decoded payload and interpretation fields");
			}
			return patch;
		}

		char const* recallPolicyName(RecallPolicy policy) {
			switch (policy) {
			case RecallPolicy::Manual: return "manual";
			case RecallPolicy::Ask: return "ask";
			case RecallPolicy::AutomaticWhenStopped: return "automatic-when-stopped";
			}
			return "manual";
		}

		RecallPolicy parseRecallPolicy(std::string const& value, std::string const& path) {
			if (value == "manual") return RecallPolicy::Manual;
			if (value == "ask") return RecallPolicy::Ask;
			if (value == "automatic-when-stopped") return RecallPolicy::AutomaticWhenStopped;
			fail(CodecErrorCode::InvalidValue, path, "unknown recall policy");
		}

		template<typename T, typename Work>
		CodecResult<T> protect(Work&& work) noexcept {
			try {
				return CodecResult<T>::success(work());
			}
			catch (DecodeFailure const& failure) {
				return CodecResult<T>::failure(failure.error);
			}
			catch (Json::exception const& error) {
				return CodecResult<T>::failure({ CodecErrorCode::InvalidJson, "$", error.what() });
			}
			catch (std::exception const& error) {
				return CodecResult<T>::failure({ CodecErrorCode::InvalidValue, "$", error.what() });
			}
			catch (...) {
				return CodecResult<T>::failure({ CodecErrorCode::InvalidValue, "$", "unknown codec failure" });
			}
		}
	}

	CodecResult<std::string> SessionPatchCodec::encode(SessionPatch const& patch, CodecLimits const& limits) noexcept {
		return protect<std::string>([&] {
			auto result = patchToJson(patch, limits).dump();
			if (result.size() > limits.maxDocumentBytes) fail(CodecErrorCode::DocumentTooLarge, "$", "encoded document exceeds configured size limit");
			return result;
		});
	}

	CodecResult<SessionPatch> SessionPatchCodec::decode(std::string_view json, CodecLimits const& limits) noexcept {
		if (json.size() > limits.maxDocumentBytes) {
			return CodecResult<SessionPatch>::failure({ CodecErrorCode::DocumentTooLarge, "$", "document exceeds configured size limit" });
		}
		return protect<SessionPatch>([&] {
			auto const document = Json::parse(json.begin(), json.end());
			return patchFromJson(document, "$", limits);
		});
	}

	CodecResult<std::string> SessionPatchCodec::fingerprint(SessionPatch const& patch, CodecLimits const& limits) noexcept {
		return protect<std::string>([&] { return calculateFingerprint(patch, limits); });
	}

	CodecResult<std::string> SessionManifestCodec::encode(SessionManifest const& manifest, CodecLimits const& limits) noexcept {
		return protect<std::string>([&] {
			if (manifest.schemaVersion != CURRENT_MANIFEST_SCHEMA_VERSION) fail(CodecErrorCode::UnsupportedVersion, "$.schemaVersion", "only version 1 can be encoded");
			checkString(manifest.pluginInstanceId, "$.pluginInstanceId", limits, false);
			checkString(manifest.instanceName, "$.instanceName", limits);
			if (manifest.sounds.size() > limits.maxSounds) fail(CodecErrorCode::InvalidValue, "$.sounds", "too many sounds");
			Json binding = Json::object();
			if (manifest.binding.configuredSynthInstanceId) {
				checkString(*manifest.binding.configuredSynthInstanceId, "$.binding.configuredSynthInstanceId", limits, false);
				binding["configuredSynthInstanceId"] = *manifest.binding.configuredSynthInstanceId;
			}
			if (manifest.binding.fallbackAdaptationId) {
				checkString(*manifest.binding.fallbackAdaptationId, "$.binding.fallbackAdaptationId", limits, false);
				binding["fallbackAdaptationId"] = *manifest.binding.fallbackAdaptationId;
			}
			Json sounds = Json::array();
			std::unordered_set<std::string> soundIds;
			std::size_t estimatedPayloadTextBytes = 0;
			for (auto const& sound : manifest.sounds) {
				checkString(sound.soundId, "$.sounds[].soundId", limits, false);
				if (!soundIds.insert(sound.soundId).second) fail(CodecErrorCode::InvalidValue, "$.sounds[].soundId", "sound IDs must be unique");
				auto const encodedPayloadSize = ((sound.patch.payload.size() + 2) / 3) * 4;
				if (estimatedPayloadTextBytes >= limits.maxDocumentBytes || encodedPayloadSize > limits.maxDocumentBytes - estimatedPayloadTextBytes) {
					fail(CodecErrorCode::DocumentTooLarge, "$.sounds", "combined encoded payloads exceed the document size limit");
				}
				estimatedPayloadTextBytes += encodedPayloadSize;
				sounds.push_back({ { "soundId", sound.soundId }, { "patch", patchToJson(sound.patch, limits) } });
			}
			if (manifest.selectedSoundId && soundIds.count(*manifest.selectedSoundId) == 0) {
				fail(CodecErrorCode::InvalidValue, "$.selectedSoundId", "selected sound does not exist");
			}
			Json document {
				{ "schemaVersion", manifest.schemaVersion },
				{ "pluginInstanceId", manifest.pluginInstanceId },
				{ "instanceName", manifest.instanceName },
				{ "binding", std::move(binding) },
				{ "recallPolicy", recallPolicyName(manifest.recallPolicy) },
				{ "sounds", std::move(sounds) },
				{ "deploymentPlan", Json::array() }
			};
			if (manifest.selectedSoundId) document["selectedSoundId"] = *manifest.selectedSoundId;
			auto result = document.dump();
			if (result.size() > limits.maxDocumentBytes) fail(CodecErrorCode::DocumentTooLarge, "$", "encoded document exceeds configured size limit");
			return result;
		});
	}

	CodecResult<SessionManifest> SessionManifestCodec::decode(std::string_view json, CodecLimits const& limits) noexcept {
		if (json.size() > limits.maxDocumentBytes) {
			return CodecResult<SessionManifest>::failure({ CodecErrorCode::DocumentTooLarge, "$", "document exceeds configured size limit" });
		}
		return protect<SessionManifest>([&] {
			auto const document = Json::parse(json.begin(), json.end());
			if (!document.is_object()) fail(CodecErrorCode::InvalidFieldType, "$", "expected an object");
			SessionManifest manifest;
			manifest.schemaVersion = requiredVersion(document, "schemaVersion", CURRENT_MANIFEST_SCHEMA_VERSION, "$" );
			manifest.pluginInstanceId = requiredString(document, "pluginInstanceId", "$", limits, false);
			manifest.instanceName = requiredString(document, "instanceName", "$", limits);
			auto const binding = document.find("binding");
			if (binding == document.end()) fail(CodecErrorCode::MissingField, "$.binding", "required field is missing");
			if (!binding->is_object()) fail(CodecErrorCode::InvalidFieldType, "$.binding", "expected an object");
			manifest.binding.configuredSynthInstanceId = optionalString(*binding, "configuredSynthInstanceId", "$.binding", limits, false);
			manifest.binding.fallbackAdaptationId = optionalString(*binding, "fallbackAdaptationId", "$.binding", limits, false);
			manifest.recallPolicy = parseRecallPolicy(requiredString(document, "recallPolicy", "$", limits, false), "$.recallPolicy");
			manifest.selectedSoundId = optionalString(document, "selectedSoundId", "$", limits, false);
			auto const sounds = document.find("sounds");
			if (sounds == document.end()) fail(CodecErrorCode::MissingField, "$.sounds", "required field is missing");
			if (!sounds->is_array()) fail(CodecErrorCode::InvalidFieldType, "$.sounds", "expected an array");
			if (sounds->size() > limits.maxSounds) fail(CodecErrorCode::InvalidValue, "$.sounds", "too many sounds");
			std::unordered_set<std::string> soundIds;
			for (std::size_t index = 0; index < sounds->size(); ++index) {
				auto const& sound = (*sounds)[index];
				auto const path = "$.sounds[" + std::to_string(index) + "]";
				if (!sound.is_object()) fail(CodecErrorCode::InvalidFieldType, path, "expected an object");
				auto soundId = requiredString(sound, "soundId", path, limits, false);
				if (!soundIds.insert(soundId).second) fail(CodecErrorCode::InvalidValue, path + ".soundId", "sound IDs must be unique");
				auto const patch = sound.find("patch");
				if (patch == sound.end()) fail(CodecErrorCode::MissingField, path + ".patch", "required field is missing");
				manifest.sounds.push_back({ std::move(soundId), patchFromJson(*patch, path + ".patch", limits) });
			}
			if (manifest.selectedSoundId && soundIds.count(*manifest.selectedSoundId) == 0) {
				fail(CodecErrorCode::InvalidValue, "$.selectedSoundId", "selected sound does not exist");
			}
			return manifest;
		});
	}
}
