#pragma once

/**
 * @file SessionState.h
 * @brief The lifecycle a collaboration session moves through, so the engine and
 *        bindings share one vocabulary for "where are we" between connect and
 *        teardown.
 */

#include <string_view>

namespace idtxflow
{
namespace net
{
namespace model
{
    enum class SessionState
    {
        Inactive,   ///< no session; idle
        Creating,   ///< session requested / socket connecting
        Active,     ///< handshake complete; edits flow
        Leaving,    ///< teardown in progress
        Error       ///< terminated by a failure
    };

    constexpr std::string_view to_string(SessionState state) noexcept
    {
        switch (state)
        {
            case SessionState::Inactive: return "Inactive";
            case SessionState::Creating: return "Creating";
            case SessionState::Active:   return "Active";
            case SessionState::Leaving:  return "Leaving";
            case SessionState::Error:    return "Error";
            default:                     return "Inactive";
        }
    }

} // namespace model
} // namespace net
} // namespace idtxflow
