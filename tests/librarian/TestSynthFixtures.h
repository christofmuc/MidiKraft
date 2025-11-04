#pragma once

#include "JuceHeader.h"

#include "Synth.h"
#include "HasBanksCapability.h"
#include "PatchHolder.h"
#include "AutomaticCategory.h"
#include "MidiProgramNumber.h"
#include "MidiBankNumber.h"

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace midikraft::test {

class TestSynth : public Synth, public HasBanksCapability {
public:
	static constexpr int kDataType = 99;

	TestSynth(std::string name, int bankCount = 4, int bankSize = 32)
		: name_(std::move(name))
		, bankCount_(bankCount)
		, bankSize_(bankSize) {}

	std::string getName() const override {
		return name_;
	}

	std::shared_ptr<DataFile> patchFromPatchData(const Synth::PatchData &data, MidiProgramNumber) const override {
		return std::make_shared<DataFile>(kDataType, data);
	}

	bool isOwnSysex(juce::MidiMessage const &message) const override {
		return message.isSysEx();
	}

	TPatchVector loadSysex(std::vector<juce::MidiMessage> const &sysexMessages) override {
		TPatchVector patches;
		for (auto const &msg : sysexMessages) {
			auto const *raw = msg.getRawData();
			auto size = msg.getRawDataSize();
			Synth::PatchData data(raw, raw + size);
			patches.push_back(std::make_shared<DataFile>(kDataType, data));
		}
		return patches;
	}

	std::vector<juce::MidiMessage> dataFileToSysex(std::shared_ptr<DataFile> dataFile, std::shared_ptr<SendTarget>) override {
		auto payload = dataFile->data();
		if (!payload.empty() && payload.front() == 0xf0) {
			payload.erase(payload.begin());
		}
		if (!payload.empty() && payload.back() == 0xf7) {
			payload.pop_back();
		}
		return { juce::MidiMessage::createSysExMessage(payload.data(), static_cast<int>(payload.size())) };
	}

	// HasBanksCapability
	int numberOfBanks() const override { return bankCount_; }
	int numberOfPatches() const override { return bankSize_; }
	std::string friendlyBankName(MidiBankNumber bankNo) const override {
		return "Bank " + std::to_string(bankNo.toOneBased());
	}
	std::vector<juce::MidiMessage> bankSelectMessages(MidiBankNumber) const override {
		return {};
	}

private:
	std::string name_;
	int bankCount_;
	int bankSize_;
};

inline std::shared_ptr<TestSynth> makeTestSynth(std::string name = "TestSynth", int bankCount = 4, int bankSize = 32) {
	return std::make_shared<TestSynth>(std::move(name), bankCount, bankSize);
}

inline std::vector<uint8> makeSysexPayload(std::initializer_list<uint8> payload) {
	std::vector<uint8> data;
	data.reserve(payload.size() + 2);
	data.push_back(0xf0);
	data.insert(data.end(), payload);
	data.push_back(0xf7);
	return data;
}

inline std::vector<uint8> defaultSysexData() {
	return makeSysexPayload({ 0x7d, 0x01, 0x02, 0x03 });
}

using CategoryMap = std::map<std::string, Category>;

inline CategoryMap makeCategoryMap() {
	const std::vector<std::string> names = {
		"Lead", "Pad", "Brass", "Organ", "Keys", "Bass", "Arp", "Pluck",
		"Drone", "Drum", "Bell", "SFX", "Ambient", "Wind", "Voice"
	};
	CategoryMap result;
	int id = 1;
	for (auto const &name : names) {
		auto def = std::make_shared<CategoryDefinition>();
		def->id = id;
		def->isActive = true;
		def->name = name;
		def->color = juce::Colour::fromRGB(static_cast<uint8>((id * 41) % 255), static_cast<uint8>((id * 59) % 255), static_cast<uint8>((id * 83) % 255));
		def->sort_order = id;
		result.emplace(name, Category(def));
		++id;
	}
	return result;
}

inline std::vector<Category> categoryVector(CategoryMap const &map) {
	std::vector<Category> categories;
	categories.reserve(map.size());
	for (auto const &entry : map) {
		categories.push_back(entry.second);
	}
	return categories;
}

inline std::vector<uint8> uniqueSysexForProgram(int programIndex) {
	uint8 payload = static_cast<uint8>((programIndex % 0x40) + 1);
	return makeSysexPayload({ payload });
}

inline PatchHolder makePatchHolder(std::shared_ptr<TestSynth> synth,
	std::string const &name,
	MidiBankNumber bank,
	int zeroBasedProgram,
	std::vector<uint8> sysex = {},
	std::shared_ptr<AutomaticCategory> detector = nullptr)
{
	if (sysex.empty()) {
		sysex = uniqueSysexForProgram(zeroBasedProgram);
	}
	auto patchData = std::make_shared<DataFile>(TestSynth::kDataType, sysex);
	auto program = MidiProgramNumber::fromZeroBaseWithBank(bank, zeroBasedProgram);
	auto source = std::make_shared<FromFileSource>(name + ".syx", name, program);
	PatchHolder holder(synth, source, patchData, detector);
	holder.setName(name);
	holder.setBank(bank);
	holder.setPatchNumber(program);
	return holder;
}

} // namespace midikraft::test
