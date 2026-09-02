#include "SessionCodecs.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

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

	std::string fixture(char const* name) {
		std::ifstream input(std::string(MIDIKRAFT_SESSION_FIXTURES_DIR) + "/" + name, std::ios::binary);
		if (!input) throw std::runtime_error(std::string("could not open fixture ") + name);
		std::ostringstream content;
		content << input.rdbuf();
		return content.str();
	}

	SessionPatch samplePatch() {
		SessionPatch patch;
		patch.adaptationId = "Oberheim Matrix 1000";
		patch.dataTypeId = "single-program";
		patch.name = "Warm Bass";
		patch.fingerprint = "sha256:d651ff451aef53a00faf1a1c65b2d65c07aa0b7a57cf92f0aec76b6d6f0f6020";
		patch.payload = { 0xf0, 0x01, 0x02, 0xf7 };
		patch.source = PatchProvenance { "patch-42", 1, 23 };
		return patch;
	}

	void validManifestRoundTripsSemantically() {
		auto const decoded = SessionManifestCodec::decode(fixture("valid-manifest.json"));
		CHECK(decoded);
		if (!decoded) return;
		CHECK(decoded.value().sounds.size() == 1);
		CHECK(decoded.value().sounds[0].patch.payload == std::vector<std::uint8_t>({ 0xf0, 0x01, 0x02, 0xf7 }));
		CHECK(decoded.value().sounds[0].patch.source.has_value());

		auto const encoded = SessionManifestCodec::encode(decoded.value());
		CHECK(encoded);
		if (!encoded) return;
		auto const decodedAgain = SessionManifestCodec::decode(encoded.value());
		CHECK(decodedAgain);
		if (decodedAgain) CHECK(decodedAgain.value() == decoded.value());
	}

	void minimalPatchNeedsNoProvenance() {
		auto const decoded = SessionPatchCodec::decode(fixture("minimal-patch.json"));
		CHECK(decoded);
		if (!decoded) return;
		CHECK(!decoded.value().source.has_value());
		CHECK(decoded.value().payload == std::vector<std::uint8_t>({ 0xf0, 0x01, 0x02, 0xf7 }));
		CHECK(decoded.value().adaptationId == "Oberheim Matrix 1000");
	}

	void unknownFieldsAreIgnored() {
		auto const decoded = SessionManifestCodec::decode(fixture("unknown-fields-manifest.json"));
		CHECK(decoded);
		if (decoded) CHECK(decoded.value().sounds.size() == 1);
	}

	void corruptDataAndFutureVersionsAreRejected() {
		auto corrupt = SessionPatchCodec::decode(fixture("corrupt-patch.json"));
		CHECK(!corrupt);
		if (!corrupt) CHECK(corrupt.error().code == CodecErrorCode::InvalidBase64);

		auto patchJson = nlohmann::json::parse(fixture("minimal-patch.json"));
		patchJson["formatVersion"] = 2;
		auto futurePatch = SessionPatchCodec::decode(patchJson.dump());
		CHECK(!futurePatch);
		if (!futurePatch) CHECK(futurePatch.error().code == CodecErrorCode::UnsupportedVersion);

		auto manifestJson = nlohmann::json::parse(fixture("valid-manifest.json"));
		manifestJson["schemaVersion"] = 2;
		auto futureManifest = SessionManifestCodec::decode(manifestJson.dump());
		CHECK(!futureManifest);
		if (!futureManifest) CHECK(futureManifest.error().code == CodecErrorCode::UnsupportedVersion);
	}

	void fingerprintsAreDeterministicAndInterpretationSensitive() {
		auto patch = samplePatch();
		auto first = SessionPatchCodec::fingerprint(patch);
		CHECK(first);
		if (!first) return;
		CHECK(first.value() == patch.fingerprint);

		patch.name = "Renamed";
		patch.source.reset();
		auto cosmeticChange = SessionPatchCodec::fingerprint(patch);
		CHECK(cosmeticChange);
		if (cosmeticChange) CHECK(cosmeticChange.value() == first.value());

		patch.dataTypeId = "different-type";
		auto interpretationChange = SessionPatchCodec::fingerprint(patch);
		CHECK(interpretationChange);
		if (interpretationChange) CHECK(interpretationChange.value() != first.value());

		patch = samplePatch();
		patch.payload.back() = 0xf6;
		auto payloadChange = SessionPatchCodec::fingerprint(patch);
		CHECK(payloadChange);
		if (payloadChange) CHECK(payloadChange.value() != first.value());
	}

	void fingerprintMismatchIsRejected() {
		auto document = nlohmann::json::parse(fixture("minimal-patch.json"));
		document["payload"] = "8AED9w==";
		auto decoded = SessionPatchCodec::decode(document.dump());
		CHECK(!decoded);
		if (!decoded) CHECK(decoded.error().code == CodecErrorCode::FingerprintMismatch);
	}

	void sizeLimitsAreAppliedBeforeUnboundedParsing() {
		CodecLimits documentLimits;
		documentLimits.maxDocumentBytes = 8;
		auto document = SessionPatchCodec::decode(fixture("minimal-patch.json"), documentLimits);
		CHECK(!document);
		if (!document) CHECK(document.error().code == CodecErrorCode::DocumentTooLarge);

		CodecLimits payloadLimits;
		payloadLimits.maxPayloadBytes = 3;
		auto payload = SessionPatchCodec::decode(fixture("minimal-patch.json"), payloadLimits);
		CHECK(!payload);
		if (!payload) CHECK(payload.error().code == CodecErrorCode::PayloadTooLarge);
	}

	void payloadsAreNotLimitedByMetadataStringSize() {
		auto patch = samplePatch();
		patch.payload.assign(70 * 1024, 0x55);
		patch.fingerprint.clear();
		auto fingerprint = SessionPatchCodec::fingerprint(patch);
		CHECK(fingerprint);
		if (!fingerprint) return;
		patch.fingerprint = fingerprint.value();
		auto encoded = SessionPatchCodec::encode(patch);
		CHECK(encoded);
		if (!encoded) return;
		auto decoded = SessionPatchCodec::decode(encoded.value());
		CHECK(decoded);
		if (decoded) CHECK(decoded.value() == patch);
	}

	void publicBoundaryReturnsErrorsInsteadOfThrowing() {
		auto malformed = SessionManifestCodec::decode("{ this is not JSON }");
		CHECK(!malformed);
		if (!malformed) CHECK(malformed.error().code == CodecErrorCode::InvalidJson);

		auto patch = samplePatch();
		patch.fingerprint = "sha256:wrong";
		auto encoded = SessionPatchCodec::encode(patch);
		CHECK(!encoded);
		if (!encoded) CHECK(encoded.error().code == CodecErrorCode::FingerprintMismatch);
	}
}

namespace configured_synth_tests {
	int failures = 0;
	void run();
}

int main() {
	configured_synth_tests::run();
	validManifestRoundTripsSemantically();
	minimalPatchNeedsNoProvenance();
	unknownFieldsAreIgnored();
	corruptDataAndFutureVersionsAreRejected();
	fingerprintsAreDeterministicAndInterpretationSensitive();
	fingerprintMismatchIsRejected();
	sizeLimitsAreAppliedBeforeUnboundedParsing();
	payloadsAreNotLimitedByMetadataStringSize();
	publicBoundaryReturnsErrorsInsteadOfThrowing();
	failures += configured_synth_tests::failures;
	if (failures != 0) std::cerr << failures << " session test(s) failed\n";
	return failures == 0 ? 0 : 1;
}
