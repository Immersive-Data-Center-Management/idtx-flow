#pragma once

/**
 * @file CollabObserver.h
 * @brief The single sink through which the collaboration engine reports results
 *        to its host. A binding implements this interface once and converts each
 *        callback into engine-native events (e.g. Godot signals).
 *
 * Every method is invoked on the host's main thread — the engine marshals
 * background results through the dispatcher before calling here — so
 * implementations may touch engine objects directly.
 */

#include <string>
#include <vector>

#include <idtxflow/net/model/CloseReason.h>
#include <idtxflow/net/model/Types.h>

namespace idtxflow
{
namespace net
{
    /// Which REST operation an error belongs to, so a host can route failures.
    enum class Op { Login, ListFiles, CreateSession, DeleteSession, Health };

    class CollabObserver
    {
    public:
        virtual ~CollabObserver() = default;

        // --- REST results ---
        virtual void on_login_ok(const model::LoginResult& result) = 0;
        virtual void on_health(const model::HealthResult& result) = 0;
        virtual void on_files(const std::vector<model::FileEntry>& files) = 0;
        virtual void on_session_created(const model::SessionInfo& session) = 0;
        virtual void on_request_failed(Op op, const model::RestError& error) = 0;

        // --- high-level session flow ---
        // Reported once a server import session has been created and its socket
        // opened: the host performs the engine-specific stage load from
        // `stage_url` (the resolved authenticated download URL), then attaches
        // the loaded stage back via attach_stage(remote=true). `ws_url` is the
        // full socket URL the flow used, surfaced for host-side logging.
        virtual void on_session_ready(const model::SessionInfo& session,
                                      const std::string& stage_url,
                                      const std::string& ws_url) = 0;
        // Reported once end_session() has torn the session down (socket closed,
        // stage detached, delete requested). Fired even when nothing was active.
        virtual void on_session_closed(const std::string& session_id) = 0;

        // --- session socket lifecycle ---
        virtual void on_socket_opened() = 0;
        virtual void on_handshake(const std::string& session_id,
                                  const std::string& usd_path,
                                  const std::string& usd_uri) = 0;
        virtual void on_remote_edit(const model::PrimEdit& edit,
                                    const std::string& from_client_id) = 0;
        virtual void on_ack(bool ok, const std::string& error) = 0;
        virtual void on_socket_error(const std::string& code, const std::string& message) = 0;
        virtual void on_disconnected(model::CloseReason reason, int code,
                                     const std::string& text) = 0;
    };

} // namespace net
} // namespace idtxflow
