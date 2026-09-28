#pragma once

/**
 * @file CloseReason.h
 * @brief Classifies why a collaboration session ended, from the two ways the
 *        backend surfaces it: a WebSocket close frame (code + reason text) and a
 *        rejected/failed upgrade reported as a transport error.
 *
 * Interpreting these codes and reason strings is a protocol concern, so it lives
 * in the core rather than in engine scripts; the categories below let a binding
 * render the same messages it does today without re-parsing raw text.
 */

#include <string>
#include <string_view>

namespace idtxflow
{
namespace net
{
namespace model
{
    enum class CloseReason
    {
        normal,               ///< clean shutdown (e.g. code 1000, or a local close)
        single_edit_busy,     ///< a single-edit session already holds the one seat
        session_gone,         ///< the session no longer exists on the backend
        missing_session_id,   ///< the upgrade lacked a usable session id
        transport_error,      ///< the socket failed/was rejected before a clean close
        unknown               ///< a close that maps to none of the above
    };

    constexpr std::string_view to_string(CloseReason reason) noexcept
    {
        switch (reason)
        {
            case CloseReason::normal:             return "normal";
            case CloseReason::single_edit_busy:   return "single_edit_busy";
            case CloseReason::session_gone:       return "session_gone";
            case CloseReason::missing_session_id: return "missing_session_id";
            case CloseReason::transport_error:    return "transport_error";
            case CloseReason::unknown:            return "unknown";
            default:                              return "unknown";
        }
    }

    namespace detail
    {
        inline bool contains(const std::string& haystack, std::string_view needle) noexcept
        {
            return haystack.find(needle) != std::string::npos;
        }
    }

    /// Close code the backend uses to signal a single-edit lockout on the socket.
    inline constexpr int kSingleEditBusyCloseCode = 4409;

    /// Map a WebSocket close (code + reason text) to a category. The single-edit
    /// lockout arrives either as the dedicated close code or as a reason mentioning
    /// single_edit; a normal shutdown uses the standard 1000. The reason text is
    /// the backend's authoritative signal for the remaining lockout categories.
    inline CloseReason parse(int code, const std::string& reason) noexcept
    {
        if (code == kSingleEditBusyCloseCode || detail::contains(reason, "single_edit"))
        {
            return CloseReason::single_edit_busy;
        }
        if (detail::contains(reason, "session no longer exists"))
        {
            return CloseReason::session_gone;
        }
        if (detail::contains(reason, "missing session id"))
        {
            return CloseReason::missing_session_id;
        }
        if (code == 1000 || code == 1001)
        {
            return CloseReason::normal;
        }
        return CloseReason::unknown;
    }

    /// A transport failure or rejected upgrade never yields a close frame, so it
    /// is classified directly rather than through the code/reason parse above.
    inline constexpr CloseReason transport_failure() noexcept
    {
        return CloseReason::transport_error;
    }

} // namespace model
} // namespace net
} // namespace idtxflow
