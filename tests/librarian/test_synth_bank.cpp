#include <doctest/doctest.h>

#include "SynthBank.h"
#include "PatchList.h"

#include "TestSynthFixtures.h"

#include "MidiProgramNumber.h"
#include "MidiBankNumber.h"

#include <string>

namespace {

midikraft::SynthBank makeSynthBank(std::shared_ptr<midikraft::test::TestSynth> const &synth, MidiBankNumber bank)
{
	return midikraft::SynthBank("Test Bank", synth, bank);
}

} // namespace

TEST_CASE("SynthBank setPatches normalizes numbering and fills remaining slots")
{
	auto synth = midikraft::test::makeTestSynth("BankSynth", 1, 3);
	auto bankNo = MidiBankNumber::fromZeroBase(0, synth->numberOfPatches());

	auto patchA = midikraft::test::makePatchHolder(synth, "PatchA", bankNo, 2);
	auto patchB = midikraft::test::makePatchHolder(synth, "PatchB", bankNo, 0);

	auto bank = makeSynthBank(synth, bankNo);
	bank.setPatches({ patchA, patchB });

	auto patches = bank.patches();
	REQUIRE(patches.size() == 3);

	CHECK(patches[0].name() == "PatchA");
	CHECK(patches[0].patchNumber().toZeroBasedDiscardingBank() == 0);
	CHECK(patches[0].bankNumber().toZeroBased() == bankNo.toZeroBased());

	CHECK(patches[1].name() == "PatchB");
	CHECK(patches[1].patchNumber().toZeroBasedDiscardingBank() == 1);

	CHECK(patches[2].patch() == nullptr);
	CHECK(patches[2].patchNumber().toZeroBasedDiscardingBank() == 2);
}

TEST_CASE("SynthBank copyListToPosition copies compatible patches and marks dirtiness")
{
	auto synth = midikraft::test::makeTestSynth("CopySynth", 1, 4);
	auto bankNo = MidiBankNumber::fromZeroBase(0, synth->numberOfPatches());

	std::vector<midikraft::PatchHolder> initial;
	for (int i = 0; i < 4; ++i) {
		initial.push_back(midikraft::test::makePatchHolder(
			synth,
			"Initial" + std::to_string(i),
			bankNo,
			i,
			midikraft::test::makeSysexPayload({ static_cast<uint8>(0x20 + i) })));
	}

	auto bank = makeSynthBank(synth, bankNo);
	bank.setPatches(initial);

	midikraft::PatchList donor("Donor");
	auto donorPatch1 = midikraft::test::makePatchHolder(
		synth,
		"DonorOne",
		bankNo,
		0,
		midikraft::test::makeSysexPayload({ 0x60 }));
	auto donorPatch2 = midikraft::test::makePatchHolder(
		synth,
		"DonorTwo",
		bankNo,
		1,
		midikraft::test::makeSysexPayload({ 0x61 }));
	auto foreignSynth = midikraft::test::makeTestSynth("Foreign", 1, 4);
	auto foreignBank = MidiBankNumber::fromZeroBase(0, foreignSynth->numberOfPatches());
	auto foreignPatch = midikraft::test::makePatchHolder(foreignSynth, "Foreign", foreignBank, 0);

	donor.setPatches({ donorPatch1, foreignPatch, donorPatch2 });

	auto startPosition = MidiProgramNumber::fromZeroBaseWithBank(bankNo, 1);
	bank.copyListToPosition(startPosition, donor);

	auto patches = bank.patches();
	REQUIRE(patches.size() == 4);
	CHECK(patches[1].name() == "DonorOne");
	CHECK(patches[2].name() == "DonorTwo");

	CHECK(bank.isPositionDirty(1));
	CHECK(bank.isPositionDirty(2));
	CHECK_FALSE(bank.isPositionDirty(0));
	CHECK_FALSE(bank.isPositionDirty(3));
}
