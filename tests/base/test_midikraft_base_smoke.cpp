#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Additive.h"

TEST_CASE("Additive harmonics default to empty")
{
    midikraft::Additive::Harmonics harmonics;
    CHECK(harmonics.harmonics().empty());
}
