/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ConfiguredSynthInstance.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <random>
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

		void checkString(std::string const& value, std::string const& path, ConfiguredSynthCodecLimits const& limits, bool allowEmpty = true) {
			if (value.size() > limits.maxStringBytes) fail(CodecErrorCode::InvalidValue, path, "string exceeds configured size limit");
			if (!allowEmpty && value.empty()) fail(CodecErrorCode::InvalidValue, path, "string must not be empty");
		}

		bool isUuid(std::string const& value) {
			if (value.size() != 36) return false;
			for (std::size_t index = 0; index < value.size(); ++index) {
				if (index == 8 || index == 13 || index == 18 || index == 23) {
					if (value[index] != '-') return false;
				}
				else if (!std::isxdigit(static_cast<unsigned char>(value[index]))) {
					return false;
				}
			}
			return true;
		}

		std::string requiredString(Json const& object, char const* key, std::string const& path, ConfiguredSynthCodecLimits const& limits, bool allowEmpty = true) {
			auto const item = object.find(key);
			if (item == object.end()) fail(CodecErrorCode::MissingField, path + "." + key, "required field is missing");
			if (!item->is_string()) fail(CodecErrorCode::InvalidFieldType, path + "." + key, "expected a string");
			auto value = item->get<std::string>();
			checkString(value, path + "." + key, limits, allowEmpty);
			return value;
		}

		bool requiredBool(Json const& object, char const* key, std::string const& path) {
			auto const item = object.find(key);
			if (item == object.end()) fail(CodecErrorCode::MissingField, path + "." + key, "required field is missing");
			if (!item->is_boolean()) fail(CodecErrorCode::InvalidFieldType, path + "." + key, "expected a boolean");
			return item->get<bool>();
		}

		std::optional<MidiPortAssignment> optionalPort(Json const& object, char const* key, std::string const& path, ConfiguredSynthCodecLimits const& limits) {
			auto const item = object.find(key);
			if (item == object.end() || item->is_null()) return std::nullopt;
			if (!item->is_object()) fail(CodecErrorCode::InvalidFieldType, path + "." + key, "expected an object or null");
			return MidiPortAssignment {
				requiredString(*item, "identifier", path + "." + key, limits),
				requiredString(*item, "name", path + "." + key, limits)
			};
		}

		std::optional<std::int32_t> optionalChannel(Json const& object, std::string const& path) {
			auto const item = object.find("midiChannel");
			if (item == object.end() || item->is_null()) return std::nullopt;
			if (!item->is_number_integer()) fail(CodecErrorCode::InvalidFieldType, path + ".midiChannel", "expected an integer or null");
			auto const value = item->get<std::int64_t>();
			if (value < 1 || value > 16) fail(CodecErrorCode::InvalidValue, path + ".midiChannel", "MIDI channel must be between 1 and 16");
			return static_cast<std::int32_t>(value);
		}

		Json portToJson(std::optional<MidiPortAssignment> const& port, ConfiguredSynthCodecLimits const& limits, std::string const& path) {
			if (!port) return nullptr;
			checkString(port->identifier, path + ".identifier", limits);
			checkString(port->name, path + ".name", limits);
			return Json { { "identifier", port->identifier }, { "name", port->name } };
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
				return CodecResult<T>::failure({ CodecErrorCode::InvalidValue, "$", "unknown configured synth failure" });
			}
		}

		std::string randomUuid() {
			std::array<unsigned char, 16> bytes {};
			std::random_device random;
			for (auto& byte : bytes) byte = static_cast<unsigned char>(random());
			bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
			bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
			static constexpr char HEX[] = "0123456789abcdef";
			std::string result;
			result.reserve(36);
			for (std::size_t index = 0; index < bytes.size(); ++index) {
				if (index == 4 || index == 6 || index == 8 || index == 10) result.push_back('-');
				result.push_back(HEX[bytes[index] >> 4]);
				result.push_back(HEX[bytes[index] & 0x0f]);
			}
			return result;
		}
	}

	CodecResult<std::string> ConfiguredSynthInstanceCodec::encode(ConfiguredSynthInstances const& configuredSynths, ConfiguredSynthCodecLimits const& limits) noexcept {
		return protect<std::string>([&] {
			if (configuredSynths.schemaVersion != CURRENT_CONFIGURED_SYNTH_SCHEMA_VERSION) fail(CodecErrorCode::UnsupportedVersion, "$.schemaVersion", "only version 1 can be encoded");
			if (configuredSynths.instances.size() > limits.maxInstances) fail(CodecErrorCode::InvalidValue, "$.instances", "too many configured synth instances");
			Json instances = Json::array();
			std::unordered_set<std::string> ids;
			for (std::size_t index = 0; index < configuredSynths.instances.size(); ++index) {
				auto const& instance = configuredSynths.instances[index];
				auto const path = "$.instances[" + std::to_string(index) + "]";
				checkString(instance.instanceId, path + ".instanceId", limits, false);
				if (!isUuid(instance.instanceId)) fail(CodecErrorCode::InvalidValue, path + ".instanceId", "expected a UUID");
				if (!ids.insert(instance.instanceId).second) fail(CodecErrorCode::InvalidValue, path + ".instanceId", "instance IDs must be unique");
				checkString(instance.displayName, path + ".displayName", limits, false);
				checkString(instance.adaptationId, path + ".adaptationId", limits, false);
				if (instance.midiChannel && (*instance.midiChannel < 1 || *instance.midiChannel > 16)) fail(CodecErrorCode::InvalidValue, path + ".midiChannel", "MIDI channel must be between 1 and 16");
				instances.push_back({
					{ "instanceId", instance.instanceId },
					{ "displayName", instance.displayName },
					{ "adaptationId", instance.adaptationId },
					{ "midiInput", portToJson(instance.midiInput, limits, path + ".midiInput") },
					{ "midiOutput", portToJson(instance.midiOutput, limits, path + ".midiOutput") },
					{ "midiChannel", instance.midiChannel ? Json(*instance.midiChannel) : Json(nullptr) },
					{ "online", instance.online },
					{ "capabilities", {
						{ "editBuffer", instance.capabilities.editBuffer },
						{ "programDump", instance.capabilities.programDump },
						{ "customProgramChange", instance.capabilities.customProgramChange },
						{ "verification", instance.capabilities.verification }
					} }
				});
			}
			auto result = Json { { "schemaVersion", configuredSynths.schemaVersion }, { "instances", std::move(instances) } }.dump();
			if (result.size() > limits.maxDocumentBytes) fail(CodecErrorCode::DocumentTooLarge, "$", "encoded document exceeds configured size limit");
			return result;
		});
	}

	CodecResult<ConfiguredSynthInstances> ConfiguredSynthInstanceCodec::decode(std::string_view json, ConfiguredSynthCodecLimits const& limits) noexcept {
		if (json.size() > limits.maxDocumentBytes) return CodecResult<ConfiguredSynthInstances>::failure({ CodecErrorCode::DocumentTooLarge, "$", "document exceeds configured size limit" });
		return protect<ConfiguredSynthInstances>([&] {
			auto const document = Json::parse(json.begin(), json.end());
			if (!document.is_object()) fail(CodecErrorCode::InvalidFieldType, "$", "expected an object");
			auto const version = document.find("schemaVersion");
			if (version == document.end()) fail(CodecErrorCode::MissingField, "$.schemaVersion", "required version field is missing");
			if (!version->is_number_unsigned()) fail(CodecErrorCode::InvalidFieldType, "$.schemaVersion", "expected an unsigned integer");
			if (version->get<std::uint64_t>() != CURRENT_CONFIGURED_SYNTH_SCHEMA_VERSION) fail(CodecErrorCode::UnsupportedVersion, "$.schemaVersion", "only version 1 is supported");
			auto const instances = document.find("instances");
			if (instances == document.end()) fail(CodecErrorCode::MissingField, "$.instances", "required field is missing");
			if (!instances->is_array()) fail(CodecErrorCode::InvalidFieldType, "$.instances", "expected an array");
			if (instances->size() > limits.maxInstances) fail(CodecErrorCode::InvalidValue, "$.instances", "too many configured synth instances");
			ConfiguredSynthInstances result;
			std::unordered_set<std::string> ids;
			for (std::size_t index = 0; index < instances->size(); ++index) {
				auto const& item = (*instances)[index];
				auto const path = "$.instances[" + std::to_string(index) + "]";
				if (!item.is_object()) fail(CodecErrorCode::InvalidFieldType, path, "expected an object");
				ConfiguredSynthInstance instance;
				instance.instanceId = requiredString(item, "instanceId", path, limits, false);
				if (!isUuid(instance.instanceId)) fail(CodecErrorCode::InvalidValue, path + ".instanceId", "expected a UUID");
				if (!ids.insert(instance.instanceId).second) fail(CodecErrorCode::InvalidValue, path + ".instanceId", "instance IDs must be unique");
				instance.displayName = requiredString(item, "displayName", path, limits, false);
				instance.adaptationId = requiredString(item, "adaptationId", path, limits, false);
				instance.midiInput = optionalPort(item, "midiInput", path, limits);
				instance.midiOutput = optionalPort(item, "midiOutput", path, limits);
				instance.midiChannel = optionalChannel(item, path);
				instance.online = requiredBool(item, "online", path);
				auto const capabilities = item.find("capabilities");
				if (capabilities == item.end()) fail(CodecErrorCode::MissingField, path + ".capabilities", "required field is missing");
				if (!capabilities->is_object()) fail(CodecErrorCode::InvalidFieldType, path + ".capabilities", "expected an object");
				instance.capabilities = {
					requiredBool(*capabilities, "editBuffer", path + ".capabilities"),
					requiredBool(*capabilities, "programDump", path + ".capabilities"),
					requiredBool(*capabilities, "customProgramChange", path + ".capabilities"),
					requiredBool(*capabilities, "verification", path + ".capabilities")
				};
				result.instances.push_back(std::move(instance));
			}
			return result;
		});
	}

	ConfiguredSynthInstanceRegistry::ConfiguredSynthInstanceRegistry(InstanceIdGenerator idGenerator)
		: idGenerator_(idGenerator ? std::move(idGenerator) : InstanceIdGenerator(randomUuid)) {}

	CodecResult<bool> ConfiguredSynthInstanceRegistry::restore(std::string_view persistedState) noexcept {
		if (persistedState.empty()) {
			configuredSynths_ = {};
			return CodecResult<bool>::success(false);
		}
		auto decoded = ConfiguredSynthInstanceCodec::decode(persistedState);
		if (!decoded) return CodecResult<bool>::failure(decoded.error());
		configuredSynths_ = std::move(decoded).value();
		for (auto& instance : configuredSynths_.instances) instance.online = false;
		return CodecResult<bool>::success(true);
	}

	CodecResult<std::string> ConfiguredSynthInstanceRegistry::serialize() const noexcept {
		return ConfiguredSynthInstanceCodec::encode(configuredSynths_);
	}

	CodecResult<std::string> ConfiguredSynthInstanceRegistry::registerInstance(std::optional<std::string> persistedInstanceId, ConfiguredSynthInstance currentState) noexcept {
		return protect<std::string>([&] {
			std::string instanceId;
			if (persistedInstanceId && !persistedInstanceId->empty()) {
				if (!isUuid(*persistedInstanceId)) fail(CodecErrorCode::InvalidValue, "$.instanceId", "persisted instance ID is not a UUID");
				instanceId = *persistedInstanceId;
			}
			else {
				instanceId = nextInstanceId();
			}

			if (auto existing = find(instanceId)) {
				if (existing->adaptationId != currentState.adaptationId) fail(CodecErrorCode::InvalidValue, "$.adaptationId", "persisted instance ID belongs to a different adaptation");
				currentState.instanceId = instanceId;
				// Runtime registration refreshes reachability, ports, and capability
				// data. The user-controlled display name is changed only via rename.
				currentState.displayName = existing->displayName;
				if (!currentState.midiInput) currentState.midiInput = existing->midiInput;
				if (!currentState.midiOutput) currentState.midiOutput = existing->midiOutput;
				if (!currentState.midiChannel) currentState.midiChannel = existing->midiChannel;
				*existing = std::move(currentState);
			}
			else {
				currentState.instanceId = instanceId;
				configuredSynths_.instances.push_back(std::move(currentState));
			}
			return instanceId;
		});
	}

	bool ConfiguredSynthInstanceRegistry::updateInstance(ConfiguredSynthInstance const& currentState) {
		auto instance = find(currentState.instanceId);
		if (!instance || instance->adaptationId != currentState.adaptationId) return false;
		auto updated = currentState;
		updated.displayName = instance->displayName;
		if (!updated.midiInput) updated.midiInput = instance->midiInput;
		if (!updated.midiOutput) updated.midiOutput = instance->midiOutput;
		if (!updated.midiChannel) updated.midiChannel = instance->midiChannel;
		*instance = std::move(updated);
		return true;
	}

	bool ConfiguredSynthInstanceRegistry::renameInstance(std::string const& instanceId, std::string displayName) {
		if (displayName.empty()) return false;
		auto instance = find(instanceId);
		if (!instance) return false;
		instance->displayName = std::move(displayName);
		return true;
	}

	ConfiguredSynthInstance const* ConfiguredSynthInstanceRegistry::find(std::string const& instanceId) const {
		auto const found = std::find_if(configuredSynths_.instances.begin(), configuredSynths_.instances.end(), [&](auto const& instance) { return instance.instanceId == instanceId; });
		return found == configuredSynths_.instances.end() ? nullptr : &*found;
	}

	ConfiguredSynthInstance* ConfiguredSynthInstanceRegistry::find(std::string const& instanceId) {
		auto const found = std::find_if(configuredSynths_.instances.begin(), configuredSynths_.instances.end(), [&](auto const& instance) { return instance.instanceId == instanceId; });
		return found == configuredSynths_.instances.end() ? nullptr : &*found;
	}

	std::vector<ConfiguredSynthInstance> const& ConfiguredSynthInstanceRegistry::all() const noexcept {
		return configuredSynths_.instances;
	}

	BindingResolution ConfiguredSynthInstanceRegistry::resolve(SynthBinding const& binding) const {
		if (!binding.configuredSynthInstanceId) return { BindingResolutionState::Unbound, nullptr };
		auto instance = find(*binding.configuredSynthInstanceId);
		return instance ? BindingResolution { BindingResolutionState::Bound, instance } : BindingResolution { BindingResolutionState::RebindRequired, nullptr };
	}

	bool ConfiguredSynthInstanceRegistry::rebind(SynthBinding& binding, std::string const& targetInstanceId, std::optional<std::string> const& expectedAdaptationId) const {
		auto target = find(targetInstanceId);
		if (!target) return false;
		auto const adaptation = expectedAdaptationId ? expectedAdaptationId : binding.fallbackAdaptationId;
		if (adaptation && *adaptation != target->adaptationId) return false;
		binding.configuredSynthInstanceId = targetInstanceId;
		binding.fallbackAdaptationId = target->adaptationId;
		return true;
	}

	std::string ConfiguredSynthInstanceRegistry::nextInstanceId() {
		for (int attempt = 0; attempt < 100; ++attempt) {
			auto candidate = idGenerator_();
			if (!isUuid(candidate)) throw std::runtime_error("instance ID generator returned an invalid UUID");
			if (!find(candidate)) return candidate;
		}
		throw std::runtime_error("instance ID generator repeatedly returned duplicate UUIDs");
	}

}
