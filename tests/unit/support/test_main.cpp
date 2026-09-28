/**
 * @file test_main.cpp
 * @brief Runner for the in-tree unit tests.
 *
 * Executes every test registered with TEST(...) via test_framework.h, prints a
 * per-case pass/fail line, and returns a non-zero exit code when any assertion
 * failed so `scons test` (and CI) can gate on it.
 *
 * Test cases live in sibling *Tests.cpp files (e.g. CodecTests.cpp) and are added
 * to the `scons test` sources alongside the logic they cover (the codec, the REST
 * client, the session socket, the engine, and the matrix conventions).
 */

#include "test_framework.h"

#include <iostream>

int main()
{
    auto& cases = idtxflow::test::registry();

    int failed_cases = 0;
    for (const auto& tc : cases)
    {
        int failures = 0;
        tc.body(failures);

        const bool ok = (failures == 0);
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << tc.suite << "." << tc.name << "\n";
        if (!ok)
        {
            ++failed_cases;
        }
    }

    std::cout << "\n"
              << (cases.size() - static_cast<size_t>(failed_cases)) << "/" << cases.size()
              << " tests passed.\n";

    return failed_cases == 0 ? 0 : 1;
}
