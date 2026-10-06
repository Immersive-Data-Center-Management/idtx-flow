#pragma once

/**
 * @file RestCodec.h
 * @brief Translates REST response/request bytes to and from the domain model.
 *
 * This is the only place the JSON dependency lives for the REST path (mirroring
 * how wire/WireCodec isolates protobuf for the socket path). Orchestration code
 * hands raw bodies here and gets back model types, and asks here to build
 * request bodies — it never sees a JSON type.
 */

#include <string>
#include <vector>

#include <idtxflow/net/model/Types.h>
#include <idtxflow/utils/Logger.h>

namespace idtxflow
{
namespace net
{
namespace adapters
{
/// REST byte <-> model translation. Parsing is lenient: a body that is not
/// the expected shape yields false (parse_*) or an empty collection, leaving
/// orchestration to decide how to report it.
class RestCodec
{
  public:
    /// Parse a login response; false if no access token is present.
    static bool parse_login(const std::string& body, model::LoginResult& out);

    /// Parse the file listing (the "files" array); empty if absent.
    static bool parse_files(const std::string& body, std::vector<model::FileEntry>& out);

    /// Parse a session response; false if no session id is present.
    static bool parse_session(const std::string& body, model::SessionInfo& out);

    /// Parse the session listing (the "sessions" array); empty if absent.
    static bool parse_sessions(const std::string& body, std::vector<model::SessionInfo>& out);

    /// Parse a commit response { committed, session_id }; false if the body
    /// is not a JSON object.
    static bool parse_commit(const std::string& body, model::CommitResult& out);

    /// Map an HTTP status + body (+ transport error text) to a RestError.
    /// http_code 0 denotes a transport failure with no response.
    static model::RestError parse_error(int http_code, const std::string& body, const std::string& transport_err);

    /// Build the JSON body for POST /auth/login.
    static std::string make_login_body(const std::string& username, const std::string& password);

    /// Build the JSON body for POST /sessions (mode defaults to single_edit;
    /// auto_commit defaults to false).
    static std::string make_session_body(const std::string& usd_file, const std::string& mode,
                                         bool auto_commit = false);

  private:
    IDTX_LOG_CATEGORY("RestCodec")
};

} // namespace adapters
} // namespace net
} // namespace idtxflow
