#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "JuceHeader.h"
#include "PatchInterchangeFormat.h"

#include "AutomaticCategory.h"
#include "Category.h"
#include "PatchHolder.h"
#include "JsonSerialization.h"
#include "HasBanksCapability.h"
#include "SynthBank.h"
#include "MidiProgramNumber.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using midikraft::Category;
using midikraft::CategoryDefinition;

class TestSynth : public midikraft::Synth, public midikraft::HasBanksCapability {
public:
	static constexpr int kDataType = 99;

	explicit TestSynth(std::string name, int bankCount = 4, int bankSize = 32)
		: name_(std::move(name)), bankCount_(bankCount), bankSize_(bankSize) {}

	std::string getName() const override {
		return name_;
	}

	std::shared_ptr<midikraft::DataFile> patchFromPatchData(const Synth::PatchData &data, MidiProgramNumber) const override {
		return std::make_shared<midikraft::DataFile>(kDataType, data);
	}

	bool isOwnSysex(juce::MidiMessage const &message) const override {
		return message.isSysEx();
	}

	midikraft::TPatchVector loadSysex(std::vector<juce::MidiMessage> const &sysexMessages) override {
		midikraft::TPatchVector patches;
		for (auto const &msg : sysexMessages) {
			auto const *raw = msg.getRawData();
			auto size = msg.getRawDataSize();
			Synth::PatchData data(raw, raw + size);
			patches.push_back(std::make_shared<midikraft::DataFile>(kDataType, data));
		}
		return patches;
	}

	std::vector<juce::MidiMessage> dataFileToSysex(std::shared_ptr<midikraft::DataFile> dataFile, std::shared_ptr<midikraft::SendTarget>) override {
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
		if (bankNo.isValid()) {
			return "Bank " + std::to_string(bankNo.toOneBased());
		}
		return "Bank ?";
	}
	std::vector<juce::MidiMessage> bankSelectMessages(MidiBankNumber) const override {
		return {};
	}

private:
	std::string name_;
	int bankCount_;
	int bankSize_;
};

std::filesystem::path createTempPath(std::string const &suffix) {
	auto base = std::filesystem::temp_directory_path();
	auto filename = "midikraft_pif_" + juce::Uuid().toString().toStdString() + suffix;
	return base / filename;
}

std::vector<uint8> testSysexData() {
	return { 0xf0, 0x7d, 0x01, 0x02, 0x03, 0xf7 };
}

using CategoryMap = std::map<std::string, Category>;

CategoryMap makeCategoryMap() {
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

std::vector<Category> categoryVector(CategoryMap const &map) {
	std::vector<Category> categories;
	categories.reserve(map.size());
	for (auto const &entry : map) {
		categories.push_back(entry.second);
	}
	return categories;
}

} // namespace

TEST_CASE("PatchInterchangeFormat::save writes rich patch metadata to JSON")
{
	auto synth = std::make_shared<TestSynth>("TestSynth");
	auto data = testSysexData();
	auto patchData = std::make_shared<midikraft::DataFile>(TestSynth::kDataType, data);
	auto sourceInfo = std::make_shared<midikraft::FromFileSource>("input.syx", "/tmp/input.syx", MidiProgramNumber::fromZeroBase(4));

	auto categories = makeCategoryMap();

	midikraft::PatchHolder holder(synth, sourceInfo, patchData);
	holder.setName("Bright Pad");
	holder.setFavorite(midikraft::Favorite(true));
	holder.setPatchNumber(MidiProgramNumber::fromZeroBase(42));
	holder.setBank(MidiBankNumber::fromZeroBase(3, synth->numberOfPatches()));
	holder.setCategory(categories.at("Pad"), true);
	holder.setUserDecision(categories.at("Pad"));
	holder.setUserDecision(categories.at("SFX"));
	holder.setComment("Very shiny");
	holder.setAuthor("Unit Tester");
	holder.setInfo("Created for tests");

	std::vector<midikraft::PatchHolder> patches{ holder };

	auto path = createTempPath(".json");
	midikraft::PatchInterchangeFormat::save(patches, path.string());

	auto doc = [&]() {
		std::ifstream input(path);
		REQUIRE(input.good());
		return nlohmann::json::parse(input);
	}();
	std::filesystem::remove(path);

	REQUIRE(doc.contains("Header"));
	auto const &header = doc["Header"];
	CHECK(header["FileFormat"] == "PatchInterchangeFormat");
	CHECK(header["Version"] == 1);

	REQUIRE(doc.contains("Library"));
	auto const &library = doc["Library"];
	REQUIRE(library.is_array());
	REQUIRE(library.size() == 1);
	auto const &entry = library[0];

	CHECK(entry["Synth"] == synth->getName());
	CHECK(entry["Name"] == "Bright Pad");
	CHECK(entry["Favorite"] == 1);
	CHECK(entry["Bank"] == 3);
	CHECK(entry["Place"] == 42);
	REQUIRE(entry["Categories"].is_array());
	CHECK(entry["Categories"] == nlohmann::json::array({ "Pad" }));
	REQUIRE(entry["NonCategories"].is_array());
	CHECK(entry["NonCategories"] == nlohmann::json::array({ "SFX" }));
	CHECK(entry["Comment"] == "Very shiny");
	CHECK(entry["Author"] == "Unit Tester");
	CHECK(entry["Info"] == "Created for tests");

	auto expectedSysex = midikraft::JsonSerialization::dataToString(data);
	CHECK(entry["Sysex"] == expectedSysex);

	REQUIRE(entry["SourceInfo"].is_object());
	CHECK(entry["SourceInfo"]["filesource"] == true);
	CHECK(entry["SourceInfo"]["filename"] == "input.syx");
	CHECK(entry["SourceInfo"]["fullpath"] == "/tmp/input.syx");
}

TEST_CASE("PatchInterchangeFormat::load rebuilds patches, metadata, and categories")
{
	auto synth = std::make_shared<TestSynth>("TestSynth");
	auto categories = makeCategoryMap();
	auto detector = std::make_shared<midikraft::AutomaticCategory>(categoryVector(categories));
	auto sysexData = testSysexData();
	auto sourceInfo = std::make_shared<midikraft::FromFileSource>("library.syx", "/tmp/library.syx", MidiProgramNumber::fromZeroBase(12));

	nlohmann::json header = {
		{ "FileFormat", "PatchInterchangeFormat" },
		{ "Version", 1 }
	};
	nlohmann::json library = nlohmann::json::array();

	nlohmann::json firstPatch = {
		{ "Synth", synth->getName() },
		{ "Name", "Glass Pad" },
		{ "Sysex", midikraft::JsonSerialization::dataToString(sysexData) },
		{ "Favorite", 1 },
		{ "Place", "7" },
		{ "Categories", nlohmann::json::array({ "Pad", "FX" }) },
		{ "NonCategories", nlohmann::json::array({ "Bells" }) },
		{ "Comment", "Captured from hardware" },
		{ "Author", "Factory" },
		{ "Info", "Legacy import" }
	};
	firstPatch["SourceInfo"] = nlohmann::json::parse(sourceInfo->toString());

	nlohmann::json secondPatch = {
		{ "Synth", synth->getName() },
		{ "Name", "Muted Bass" },
		{ "Sysex", midikraft::JsonSerialization::dataToString(sysexData) },
		{ "Favorite", 0 },
		{ "Bank", "3" },
		{ "Place", 11 }
	};

	nlohmann::json ignoredPatch = {
		{ "Synth", "UnknownSynth" },
		{ "Name", "Skip me" },
		{ "Sysex", midikraft::JsonSerialization::dataToString(sysexData) }
	};

	library.push_back(firstPatch);
	library.push_back(secondPatch);
	library.push_back(ignoredPatch);

	nlohmann::json doc;
	doc["Header"] = header;
	doc["Library"] = library;

	auto path = createTempPath(".json");
	{
		std::ofstream out(path);
		REQUIRE(out.good());
		out << std::setw(4) << doc;
	}

	std::map<std::string, std::shared_ptr<midikraft::Synth>> activeSynths = {
		{ synth->getName(), synth }
	};

	auto loaded = midikraft::PatchInterchangeFormat::load(activeSynths, path.string(), detector);
	std::filesystem::remove(path);

	REQUIRE(loaded.size() == 2);

	auto const &first = loaded[0];
	CHECK(first.name() == "Glass Pad");
	CHECK(first.howFavorite().is() == midikraft::Favorite::TFavorite::YES);
	CHECK(first.patchNumber().isValid());
	CHECK(first.patchNumber().toZeroBasedDiscardingBank() == 7);
	CHECK(first.comment() == "Captured from hardware");
	CHECK(first.author() == "Factory");
	CHECK(first.info() == "Legacy import");

	auto firstCategories = first.categories();
	CHECK(firstCategories.find(categories.at("Pad")) != firstCategories.end());
	CHECK(firstCategories.find(categories.at("SFX")) != firstCategories.end());

	auto userDecisions = first.userDecisionSet();
	CHECK(userDecisions.find(categories.at("Pad")) != userDecisions.end());
	CHECK(userDecisions.find(categories.at("SFX")) != userDecisions.end());
	CHECK(userDecisions.find(categories.at("Bell")) != userDecisions.end());

	auto info = std::dynamic_pointer_cast<midikraft::FromFileSource>(first.sourceInfo());
	REQUIRE(info);
	CHECK(info->filename() == "library.syx");
	CHECK(info->fullpath() == "/tmp/library.syx");
	CHECK(info->programNumber().toZeroBasedDiscardingBank() == 12);

	auto firstData = first.patch()->data();
	CHECK(firstData == sysexData);

	auto const &second = loaded[1];
	CHECK(second.name() == "Muted Bass");
	CHECK(second.howFavorite().is() == midikraft::Favorite::TFavorite::NO);
	CHECK(second.bankNumber().isValid());
	CHECK(second.bankNumber().toZeroBased() == 3);
	CHECK(second.patchNumber().isValid());
	CHECK(second.patchNumber().toZeroBasedDiscardingBank() == 11);
	CHECK(second.categories().empty());
}

TEST_CASE("PatchInterchangeFormat::load rejects invalid headers and data")
{
	auto synth = std::make_shared<TestSynth>("TestSynth");
	auto detector = std::make_shared<midikraft::AutomaticCategory>(categoryVector(makeCategoryMap()));
	auto sysexData = testSysexData();

	// Invalid header
	{
		nlohmann::json doc;
		doc["Header"] = {
			{ "FileFormat", "SomethingElse" },
			{ "Version", 1 }
		};
		doc["Library"] = nlohmann::json::array({
			nlohmann::json{
				{ "Synth", synth->getName() },
				{ "Name", "Bad Header" },
				{ "Sysex", midikraft::JsonSerialization::dataToString(sysexData) }
			}
		});

		auto path = createTempPath(".json");
		{
			std::ofstream out(path);
			out << doc.dump(2);
		}
		std::map<std::string, std::shared_ptr<midikraft::Synth>> activeSynths = {
			{ synth->getName(), synth }
		};
		auto loaded = midikraft::PatchInterchangeFormat::load(activeSynths, path.string(), detector);
		std::filesystem::remove(path);
		CHECK(loaded.empty());
	}

	// Invalid base64 content -> entry skipped
	{
		nlohmann::json doc;
		doc["Header"] = {
			{ "FileFormat", "PatchInterchangeFormat" },
			{ "Version", 1 }
		};
		doc["Library"] = nlohmann::json::array({
			nlohmann::json{
				{ "Synth", synth->getName() },
				{ "Name", "Corrupt Sysex" },
				{ "Sysex", "not base64!" }
			}
		});

		auto path = createTempPath(".json");
		{
			std::ofstream out(path);
			out << doc.dump(2);
		}
		std::map<std::string, std::shared_ptr<midikraft::Synth>> activeSynths = {
			{ synth->getName(), synth }
		};
		auto loaded = midikraft::PatchInterchangeFormat::load(activeSynths, path.string(), detector);
		std::filesystem::remove(path);
		CHECK(loaded.empty());
	}
}
