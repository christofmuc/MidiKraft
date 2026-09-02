/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "SessionCodecs.h"
#include "SessionTypes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace midikraft::session {

	inline constexpr std::uint32_t CURRENT_CONFIGURED_SYNTH_SCHEMA_VERSION = 1;

	struct MidiPortAssignment {
		std::string identifier;
		std::string name;

		bool operator==(MidiPortAssignment const& other) const = default;
	};

	struct ConfiguredSynthCapabilities {
		bool editBuffer = false;
		bool programDump = false;
		bool customProgramChange = false;
		bool verification = false;

		bool operator==(ConfiguredSynthCapabilities const& other) const = default;
	};

	// instanceId identifies one user-configured piece of hardware. adaptationId
	// identifies the code that interprets its data and must not change when the
	// user renames the hardware instance.
	struct ConfiguredSynthInstance {
		std::string instanceId;
		std::string displayName;
		std::string adaptationId;
		std::optional<MidiPortAssignment> midiInput;
		std::optional<MidiPortAssignment> midiOutput;
		std::optional<std::int32_t> midiChannel;
		bool online = false;
		ConfiguredSynthCapabilities capabilities;

		bool operator==(ConfiguredSynthInstance const& other) const = default;
	};

	struct ConfiguredSynthInstances {
		std::uint32_t schemaVersion = CURRENT_CONFIGURED_SYNTH_SCHEMA_VERSION;
		std::vector<ConfiguredSynthInstance> instances;

		bool operator==(ConfiguredSynthInstances const& other) const = default;
	};

	struct ConfiguredSynthCodecLimits {
		std::size_t maxDocumentBytes = 1024 * 1024;
		std::size_t maxStringBytes = 64 * 1024;
		std::size_t maxInstances = 1024;
	};

	class ConfiguredSynthInstanceCodec {
	public:
		[[nodiscard]] static CodecResult<std::string> encode(
			ConfiguredSynthInstances const& configuredSynths,
			ConfiguredSynthCodecLimits const& limits = {}) noexcept;
		[[nodiscard]] static CodecResult<ConfiguredSynthInstances> decode(
			std::string_view json,
			ConfiguredSynthCodecLimits const& limits = {}) noexcept;
	};

	enum class BindingResolutionState {
		Unbound,
		Bound,
		RebindRequired
	};

	struct BindingResolution {
		BindingResolutionState state = BindingResolutionState::Unbound;
		ConfiguredSynthInstance const* instance = nullptr;
	};

	class ConfiguredSynthInstanceRegistry {
	public:
		using InstanceIdGenerator = std::function<std::string()>;

		explicit ConfiguredSynthInstanceRegistry(InstanceIdGenerator idGenerator = {});

		[[nodiscard]] CodecResult<bool> restore(std::string_view persistedState) noexcept;
		[[nodiscard]] CodecResult<std::string> serialize() const noexcept;

		// persistedInstanceId is the exact identity associated with the runtime
		// configuration slot. When absent, this is a legacy configuration and a
		// fresh ID is assigned. No adaptation-name matching is performed.
		[[nodiscard]] CodecResult<std::string> registerInstance(
			std::optional<std::string> persistedInstanceId,
			ConfiguredSynthInstance currentState) noexcept;

		[[nodiscard]] bool updateInstance(ConfiguredSynthInstance const& currentState);
		[[nodiscard]] bool renameInstance(std::string const& instanceId, std::string displayName);
		[[nodiscard]] ConfiguredSynthInstance const* find(std::string const& instanceId) const;
		[[nodiscard]] ConfiguredSynthInstance* find(std::string const& instanceId);
		[[nodiscard]] std::vector<ConfiguredSynthInstance> const& all() const noexcept;

		[[nodiscard]] BindingResolution resolve(SynthBinding const& binding) const;
		[[nodiscard]] bool rebind(
			SynthBinding& binding,
			std::string const& targetInstanceId,
			std::optional<std::string> const& expectedAdaptationId = std::nullopt) const;

	private:
		[[nodiscard]] std::string nextInstanceId();

		ConfiguredSynthInstances configuredSynths_;
		InstanceIdGenerator idGenerator_;
	};

}
