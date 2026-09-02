/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ConfiguredSynthInstance.h"

#include <iostream>
#include <string>
#include <vector>

namespace configured_synth_tests {
	using namespace midikraft::session;

	extern int failures;

	void check(bool condition, char const* expression, int line) {
		if (!condition) {
			std::cerr << "line " << line << ": configured synth check failed: " << expression << '\n';
			++failures;
		}
	}

#define SYNTH_CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

	ConfiguredSynthInstance matrix(std::string displayName) {
		ConfiguredSynthInstance instance;
		instance.displayName = std::move(displayName);
		instance.adaptationId = "Oberheim Matrix 1000";
		instance.midiInput = MidiPortAssignment { "in-1", "MIDI In" };
		instance.midiOutput = MidiPortAssignment { "out-1", "MIDI Out" };
		instance.midiChannel = 3;
		instance.online = true;
		instance.capabilities = { true, true, false, true };
		return instance;
	}

	void run() {
		std::vector<std::string> ids {
			"11111111-1111-4111-8111-111111111111",
			"22222222-2222-4222-8222-222222222222"
		};
		auto nextId = [&] { auto id = ids.front(); ids.erase(ids.begin()); return id; };
		ConfiguredSynthInstanceRegistry registry(nextId);

		// Legacy configurations have no ID. Even identical models receive distinct
		// identities instead of being collapsed by adaptation/display name.
		auto first = registry.registerInstance(std::nullopt, matrix("Rack Matrix"));
		auto second = registry.registerInstance(std::nullopt, matrix("Keyboard Matrix"));
		SYNTH_CHECK(first);
		SYNTH_CHECK(second);
		if (!first || !second) return;
		SYNTH_CHECK(first.value() != second.value());
		SYNTH_CHECK(registry.all().size() == 2);

		// Rename is cosmetic and does not affect identity.
		SYNTH_CHECK(registry.renameInstance(first.value(), "Studio Matrix"));
		SYNTH_CHECK(registry.find(first.value())->displayName == "Studio Matrix");

		// A temporary missing port is represented as offline and retains the last
		// assigned endpoints for diagnostics and later recovery.
		auto missing = *registry.find(first.value());
		missing.midiInput.reset();
		missing.midiOutput.reset();
		missing.online = false;
		SYNTH_CHECK(registry.updateInstance(missing));
		SYNTH_CHECK(registry.find(first.value())->midiInput->identifier == "in-1");
		SYNTH_CHECK(!registry.find(first.value())->online);

		// Persistence is stable across restart. Online is deliberately live state
		// and is reset until the runtime device registers again.
		auto state = registry.serialize();
		SYNTH_CHECK(state);
		ConfiguredSynthInstanceRegistry restarted([] { return "33333333-3333-4333-8333-333333333333"; });
		auto restored = restarted.restore(state.value());
		SYNTH_CHECK(restored);
		SYNTH_CHECK(restarted.find(first.value()) != nullptr);
		SYNTH_CHECK(restarted.find(first.value())->displayName == "Studio Matrix");
		SYNTH_CHECK(!restarted.find(second.value())->online);
		auto reregistered = restarted.registerInstance(first.value(), matrix("Studio Matrix"));
		SYNTH_CHECK(reregistered);
		if (reregistered) SYNTH_CHECK(reregistered.value() == first.value());

		// Lookup is UUID-only. A fallback adaptation never silently chooses either
		// of the duplicate Matrix instances.
		SynthBinding binding { "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "Oberheim Matrix 1000" };
		auto unresolved = restarted.resolve(binding);
		SYNTH_CHECK(unresolved.state == BindingResolutionState::RebindRequired);
		SYNTH_CHECK(unresolved.instance == nullptr);
		SYNTH_CHECK(restarted.rebind(binding, second.value()));
		SYNTH_CHECK(binding.configuredSynthInstanceId == second.value());
		SYNTH_CHECK(restarted.resolve(binding).state == BindingResolutionState::Bound);

		SynthBinding incompatible { std::nullopt, "Sequential Prophet 6" };
		SYNTH_CHECK(!restarted.rebind(incompatible, first.value()));

		// Capability flags remain independent; program-dump and custom program
		// change support are not inferred from edit-buffer support.
		auto capabilityInstance = *restarted.find(first.value());
		capabilityInstance.capabilities = { true, false, true, false };
		SYNTH_CHECK(restarted.updateInstance(capabilityInstance));
		auto capabilities = restarted.find(first.value())->capabilities;
		SYNTH_CHECK(capabilities.editBuffer);
		SYNTH_CHECK(!capabilities.programDump);
		SYNTH_CHECK(capabilities.customProgramChange);
		SYNTH_CHECK(!capabilities.verification);
	}
}
