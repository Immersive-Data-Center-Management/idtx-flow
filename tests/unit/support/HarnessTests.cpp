/**
 * @file HarnessTests.cpp
 * @brief Sanity checks that the in-tree harness itself registers, runs, and
 *        reports assertions correctly. Keeps `scons test` meaningful even before
 *        the core has cases of its own.
 */

#include "test_framework.h"

TEST(Harness, TrivialPasses)
{
    CHECK(true);
    CHECK_EQ(2 + 2, 4);
    CHECK_NEAR(1.0, 1.0 + 1e-12, 1e-9);
}
