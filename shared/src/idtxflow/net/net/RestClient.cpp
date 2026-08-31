#include <idtxflow/net/net/RestClient.h>

#include <cctype>

#include <idtxflow/net/adapters/rest/RestCodec.h>

namespace idtxflow
{
namespace net
{

void RestClient::set_base_url(const std::string& url)
{
    // The transport owns the base URL; keep it as the single source of truth so
    // the URL helpers below stay in sync with where requests are actually sent.
    http_->set_base_url(url);
}

std::string RestClient::ws_base_url() const
{
    std::string ws = http_->base_url();
    if (ws.rfind("https://", 0) == 0)
    {
        ws.replace(0, 8, "wss://");
    }
    else if (ws.rfind("http://", 0) == 0)
    {
        ws.replace(0, 7, "ws://");
    }
    return ws;
}

std::string RestClient::download_url(const std::string& usd_file) const
{
    return http_->base_url() + "/api/v1/download/" + usd_file;
}

std::string RestClient::url_encode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

void RestClient::report_error(const model::RestError& error, const ErrorCb& on_err)
{
    IDTX_LOG(IDTX_ERROR, "REST error http={} code='{}' msg='{}'",
             error.http_code, error.error_code, error.message);
    // A 401 on any protected call means the token is dead; clear it so the caller
    // (and any shared reader, e.g. the USD fetcher) stops sending a stale token.
    if (error.http_code == 401)
    {
        IDTX_LOG(IDTX_WARN, "401 Unauthorized — clearing stored token");
        token_->clear();
    }
    if (on_err)
    {
        on_err(error);
    }
}

bool RestClient::attach_auth(ports::IHttpTransport::Request& req, const ErrorCb& on_err)
{
    const std::string header = token_ ? token_->auth_header_value() : std::string();
    if (header.empty())
    {
        // No token: an unauthenticated attempt at a protected endpoint. Fail
        // fast without issuing a request (which would 401 and clear the token).
        // This is an expected state (e.g. not logged in yet), so it is delivered
        // to the caller but not logged as an error.
        model::RestError err;
        err.http_code = 401;
        err.error_code = "not_authenticated";
        err.message = "Not authenticated: no token; log in first.";
        dispatcher_->post([err, on_err] { if (on_err) on_err(err); });
        return false;
    }
    req.headers["Authorization"] = header;
    return true;
}

void RestClient::health(HealthCb on_ok, ErrorCb on_err)
{
    ports::IHttpTransport::Request req;
    req.method = "GET";
    req.endpoint = "/api/v1/health";
    // Unauthenticated probe: no Authorization header.

    http_->request_async(req, [this, on_ok, on_err](const ports::IHttpTransport::Response& resp)
    {
        if (!resp.ok())
        {
            model::RestError err = adapters::RestCodec::parse_error(resp.status, resp.body, resp.error);
            dispatcher_->post([this, err, on_err] { report_error(err, on_err); });
            return;
        }

        model::HealthResult hr;
        hr.ok = true;
        hr.http_code = resp.status;
        dispatcher_->post([hr, on_ok] { if (on_ok) on_ok(hr); });
    });
}

void RestClient::login(const std::string& username, const std::string& password,
                       LoginCb on_ok, ErrorCb on_err)
{
    ports::IHttpTransport::Request req;
    req.method = "POST";
    req.endpoint = "/api/v1/auth/login";
    req.body = adapters::RestCodec::make_login_body(username, password);
    req.headers["Content-Type"] = "application/json";

    http_->request_async(req, [this, on_ok, on_err](const ports::IHttpTransport::Response& resp)
    {
        if (!resp.ok())
        {
            model::RestError err = adapters::RestCodec::parse_error(resp.status, resp.body, resp.error);
            dispatcher_->post([this, err, on_err] { report_error(err, on_err); });
            return;
        }

        model::LoginResult lr;
        const bool parsed = adapters::RestCodec::parse_login(resp.body, lr);
        dispatcher_->post([this, parsed, lr, resp, on_ok, on_err]
        {
            if (parsed)
            {
                if (on_ok) on_ok(lr);
            }
            else
            {
                report_error(adapters::RestCodec::parse_error(resp.status, resp.body,
                                                              "no access_token in response"),
                             on_err);
            }
        });
    });
}

void RestClient::list_files(const std::string& name_contains, const std::string& extension,
                            FilesCb on_ok, ErrorCb on_err)
{
    std::string endpoint = "/api/v1/files";
    std::string query;
    if (!name_contains.empty())
    {
        query += (query.empty() ? "?" : "&");
        query += "name_contains=" + url_encode(name_contains);
    }
    if (!extension.empty())
    {
        query += (query.empty() ? "?" : "&");
        query += "extension=" + url_encode(extension);
    }
    endpoint += query;

    ports::IHttpTransport::Request req;
    req.method = "GET";
    req.endpoint = endpoint;
    if (!attach_auth(req, on_err)) return;

    http_->request_async(req, [this, on_ok, on_err](const ports::IHttpTransport::Response& resp)
    {
        if (!resp.ok())
        {
            model::RestError err = adapters::RestCodec::parse_error(resp.status, resp.body, resp.error);
            dispatcher_->post([this, err, on_err] { report_error(err, on_err); });
            return;
        }

        std::vector<model::FileEntry> files;
        adapters::RestCodec::parse_files(resp.body, files);
        dispatcher_->post([files, on_ok] { if (on_ok) on_ok(files); });
    });
}

void RestClient::create_session(const std::string& usd_file, const std::string& mode,
                                SessionCb on_ok, ErrorCb on_err)
{
    ports::IHttpTransport::Request req;
    req.method = "POST";
    req.endpoint = "/api/v1/sessions";
    req.body = adapters::RestCodec::make_session_body(usd_file, mode);
    req.headers["Content-Type"] = "application/json";
    if (!attach_auth(req, on_err)) return;

    http_->request_async(req, [this, on_ok, on_err](const ports::IHttpTransport::Response& resp)
    {
        if (!resp.ok())
        {
            model::RestError err = adapters::RestCodec::parse_error(resp.status, resp.body, resp.error);
            dispatcher_->post([this, err, on_err] { report_error(err, on_err); });
            return;
        }

        model::SessionInfo si;
        const bool parsed = adapters::RestCodec::parse_session(resp.body, si);
        dispatcher_->post([this, parsed, si, resp, on_ok, on_err]
        {
            if (parsed)
            {
                if (on_ok) on_ok(si);
            }
            else
            {
                report_error(adapters::RestCodec::parse_error(resp.status, resp.body,
                                                              "no session_id in response"),
                             on_err);
            }
        });
    });
}

void RestClient::delete_session(const std::string& session_id,
                                DeletedCb on_ok, ErrorCb on_err)
{
    ports::IHttpTransport::Request req;
    req.method = "DELETE";
    req.endpoint = "/api/v1/sessions/" + session_id;
    if (!attach_auth(req, on_err)) return;

    http_->request_async(req, [this, on_ok, on_err](const ports::IHttpTransport::Response& resp)
    {
        // 204 No Content on success; 404 means already gone — both are "ok".
        if (resp.ok() || resp.status == 404)
        {
            dispatcher_->post([on_ok] { if (on_ok) on_ok(); });
            return;
        }
        model::RestError err = adapters::RestCodec::parse_error(resp.status, resp.body, resp.error);
        dispatcher_->post([this, err, on_err] { report_error(err, on_err); });
    });
}

} // namespace net
} // namespace idtxflow

