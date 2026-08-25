#pragma once

/**
 * @file test_framework.h
 * @brief A tiny, dependency-free unit-test harness for the engine-agnostic core.
 *
 * It exists so the STL-only core (codec, REST parsing, session flow, coalescing)
 * can be exercised with hand-written fakes and no server, engine, or third-party
 * test dependency. Tests self-register with TEST(suite, name); the runner in
 * test_main.cpp executes them and reports pass/fail through the process exit code.
 *
 *   TEST(Codec, Mat4RoundTripIdentity) { CHECK_NEAR(a, b, 1e-9); }
 *
 * Assertions record a failure and continue, so one test can report several
 * problems in a single run.
 */

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace idtxflow
{
namespace test
{
    /// One registered test: its suite/name plus the body to run. `failures` is
    /// incremented by the assertion macros while the body executes.
    struct TestCase
    {
        using Body = void (*)(int& failures);

        std::string suite;
        std::string name;
        Body        body;
    };

    /// The process-wide registry. A function-local static keeps registration
    /// order well-defined regardless of static-initialization order.
    inline std::vector<TestCase>& registry()
    {
        static std::vector<TestCase> cases;
        return cases;
    }

    /// Registers a test at static-init time via a throwaway object.
    struct Registrar
    {
        Registrar(const char* suite, const char* name, TestCase::Body body)
        {
            registry().push_back(TestCase{suite, name, body});
        }
    };

    inline void report_failure(const char* file, int line, const std::string& message, int& failures)
    {
        std::cerr << "    FAIL " << file << ":" << line << "  " << message << "\n";
        ++failures;
    }
} // namespace test
} // namespace idtxflow

/// Define and self-register a test. The body receives an int& named
/// `idtx_failures_` that the assertion macros below feed.
#define TEST(suite, name)                                                                 \
    static void idtx_test_##suite##_##name(int& idtx_failures_);                          \
    static ::idtxflow::test::Registrar idtx_reg_##suite##_##name(                         \
        #suite, #name, &idtx_test_##suite##_##name);                                      \
    static void idtx_test_##suite##_##name(int& idtx_failures_)

/// Assert a boolean condition.
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            ::idtxflow::test::report_failure(__FILE__, __LINE__,                          \
                std::string("CHECK(") + #cond + ") is false", idtx_failures_);            \
        }                                                                                 \
    } while (0)

/// Assert equality (operands must be streamable to std::cerr).
#define CHECK_EQ(a, b)                                                                    \
    do {                                                                                  \
        auto idtx_a_ = (a);                                                               \
        auto idtx_b_ = (b);                                                               \
        if (!(idtx_a_ == idtx_b_)) {                                                      \
            std::cerr << "    FAIL " << __FILE__ << ":" << __LINE__                        \
                      << "  CHECK_EQ(" << #a << ", " << #b << ")  "                        \
                      << idtx_a_ << " != " << idtx_b_ << "\n";                             \
            ++idtx_failures_;                                                             \
        }                                                                                 \
    } while (0)

/// Assert two floating-point values are within eps of each other.
#define CHECK_NEAR(a, b, eps)                                                             \
    do {                                                                                  \
        double idtx_a_ = static_cast<double>(a);                                          \
        double idtx_b_ = static_cast<double>(b);                                          \
        double idtx_d_ = std::fabs(idtx_a_ - idtx_b_);                                    \
        if (!(idtx_d_ <= (eps))) {                                                        \
            std::cerr << "    FAIL " << __FILE__ << ":" << __LINE__                        \
                      << "  CHECK_NEAR(" << #a << ", " << #b << ")  |"                     \
                      << idtx_a_ << " - " << idtx_b_ << "| = " << idtx_d_                  \
                      << " > " << (eps) << "\n";                                           \
            ++idtx_failures_;                                                             \
        }                                                                                 \
    } while (0)
