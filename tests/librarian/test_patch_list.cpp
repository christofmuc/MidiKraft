#include <doctest/doctest.h>

#include "PatchList.h"

#include "TestSynthFixtures.h"

#include "MidiProgramNumber.h"
#include "MidiBankNumber.h"

TEST_CASE("PatchList maintains insertion order and append semantics")
{
	auto synth = midikraft::test::makeTestSynth("ListSynth");
	auto bank = MidiBankNumber::fromZeroBase(0, synth->numberOfPatches());

	auto first = midikraft::test::makePatchHolder(synth, "First", bank, 0);
	auto second = midikraft::test::makePatchHolder(synth, "Second", bank, 1);

	midikraft::PatchList list("Favorites");
	list.setPatches({ first });
	list.addPatch(second);

	auto patches = list.patches();
	REQUIRE(patches.size() == 2);
	CHECK(patches[0].name() == "First");
	CHECK(patches[1].name() == "Second");
}

TEST_CASE("PatchList insertPatchAtTopAndRemoveDuplicates replaces duplicates for same synth")
{
	auto synth = midikraft::test::makeTestSynth("DupSynth");
	auto bank = MidiBankNumber::fromZeroBase(0, synth->numberOfPatches());

	auto original = midikraft::test::makePatchHolder(synth, "Original", bank, 0);
	auto originalData = original.patch()->data();
	auto replacement = midikraft::test::makePatchHolder(
		synth,
		"Replacement",
		bank,
		0,
		originalData);

	midikraft::PatchList list("Recent");
	list.setPatches({ original });
	list.insertPatchAtTopAndRemoveDuplicates(replacement);

	auto patches = list.patches();
	REQUIRE(patches.size() == 1);
	CHECK(patches.front().name() == "Replacement");

	auto otherSynth = midikraft::test::makeTestSynth("OtherSynth");
	auto otherBank = MidiBankNumber::fromZeroBase(0, otherSynth->numberOfPatches());
	auto foreignPatch = midikraft::test::makePatchHolder(otherSynth, "Foreign", otherBank, 0);

	list.insertPatchAtTopAndRemoveDuplicates(foreignPatch);
	patches = list.patches();
	REQUIRE(patches.size() == 2);
	CHECK(patches.front().name() == "Foreign");
	CHECK(patches.back().name() == "Replacement");
}
