#include <idtxflow/net/IdtxRestClient.h>

#include <cctype>
#include <string>
#include <thread>
#include <vector>

#include <ixwebsocket/IXHttpClient.h>

#include <pxr/base/js/json.h>
#include <pxr/base/js/value.h>

#include <idtxflow/net/IdtxTokenHolder.h>
#include <idtxflow/utils/Logger.h>

namespace idtxflow
{
namespace net
{
namespace
{
    // File-scope log category (see Logger.h: IDTX_LOG uses LOG_CATEGORY).
    static constexpr const char* LOG_CATEGORY = "IdtxRestClient";

    std::string JsGetString(const pxr::JsObject& obj, const char* key)
    {
        auto it = obj.find(key);
        if (it != obj.end() && it->second.IsString())
        {
            return it->second.GetString();
        }
        return {};
    }

    int64_t JsGetInt(const pxr::JsObject& obj, const char* key)
    {
        auto it = obj.find(key);
        if (it == obj.end())
        {
            return 0;
        }
        const pxr::JsValue& v = it->second;
        if (v.IsInt())
        {
            return v.GetInt64();
        }
        if (v.IsReal())
        {
            return static_cast<int64_t>(v.GetReal());
        }
        return 0;
    }

    RestError MakeError(int http_code, const std::string& body, const std::string& transport_err)
    {
        RestError e;
        e.http_code = http_code;
        if (http_code == 0)
        {
            e.error_code = "transport_error";
            e.message = transport_err.empty() ? "network failure" : transport_err;
            return e;
        }

        pxr::JsParseError perr;
        pxr::JsValue parsed = pxr::JsParseString(body, &perr);
        if (parsed.IsObject())
        {
            const pxr::JsObject& o = parsed.GetJsObject();
            e.error_code = JsGetString(o, "error");
            e.message = JsGetString(o, "message");
        }
        if (e.error_code.empty())
        {
            e.error_code = "http_" + std::to_string(http_code);
        }
        if (e.message.empty())
        {
            e.message = body.empty() ? "request failed" : body;
        }
        return e;
    }

    ix::HttpRequestArgsPtr MakeArgs(ix::HttpClient& client, bool with_auth)
    {
        ix::HttpRequestArgsPtr args = client.createRequest();
        args->connectTimeout = 30;
        args->transferTimeout = 120;
        args->followRedirects = true;
        args->maxRedirects = 5;
        args->compress = false;
        args->extraHeaders["Accept"] = "application/json";
        if (with_auth)
        {
            const std::string auth = IdtxTokenHolder::AuthHeaderValue();
            if (!auth.empty())
            {
                args->extraHeaders["Authorization"] = auth;
            }
        }
        return args;
    }

    bool IsSuccess(const ix::HttpResponsePtr& r)
    {
        return r && r->statusCode >= 200 && r->statusCode < 300;
    }

    std::string UrlEncode(const std::string& s)
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

} // namespace

std::string IdtxRestClient::NormalizeBase(std::string url)
{
    if (!url.empty() && url.back() == '/')
    {
        url.pop_back();
    }
    return url;
}

std::string IdtxRestClient::ws_base_url() const
{
    std::string ws = base_url_;
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

std::string IdtxRestClient::download_url(const std::string& usd_file) const
{
    return base_url_ + "/api/v1/download/" + usd_file;
}

void IdtxRestClient::login(const std::string& username,
                           const std::string& password,
                           LoginCallback on_ok,
                           ErrorCallback on_err) const
{
    const std::string url = base_url_ + "/api/v1/auth/login";

    pxr::JsObject body;
    body["username"] = pxr::JsValue(username);
    body["password"] = pxr::JsValue(password);
    const std::string payload = pxr::JsWriteToString(pxr::JsValue(body));

    std::thread([url, payload, on_ok, on_err]()
    {
        ix::HttpClient client;
        ix::HttpRequestArgsPtr args = MakeArgs(client, /*with_auth*/ false);
        args->extraHeaders["Content-Type"] = "application/json";

        auto resp = client.post(url, payload, args);
        if (!IsSuccess(resp))
        {
            if (on_err)
            {
                on_err(MakeError(resp ? resp->statusCode : 0,
                                 resp ? resp->body : std::string(),
                                 resp ? resp->errorMsg : std::string("null response")));
            }
            return;
        }

        pxr::JsParseError perr;
        pxr::JsValue parsed = pxr::JsParseString(resp->body, &perr);
        if (!parsed.IsObject())
        {
            if (on_err) on_err(MakeError(resp->statusCode, resp->body, "invalid json"));
            return;
        }
        const pxr::JsObject& o = parsed.GetJsObject();

        LoginResult lr;
        lr.access_token  = JsGetString(o, "access_token");
        lr.token_type    = JsGetString(o, "token_type");
        if (lr.token_type.empty()) lr.token_type = "Bearer";
        lr.expires_in    = JsGetInt(o, "expires_in");
        lr.refresh_token = JsGetString(o, "refresh_token");
        lr.scope         = JsGetString(o, "scope");

        if (lr.access_token.empty())
        {
            if (on_err) on_err(MakeError(resp->statusCode, resp->body, "no access_token in response"));
            return;
        }
        if (on_ok) on_ok(lr);
    }).detach();
}

void IdtxRestClient::list_files(const std::string& name_contains,
                                const std::string& extension,
                                FilesCallback on_ok,
                                ErrorCallback on_err) const
{
    std::string url = base_url_ + "/api/v1/files";
    std::string query;
    if (!name_contains.empty())
    {
        query += (query.empty() ? "?" : "&");
        query += "name_contains=" + UrlEncode(name_contains);
    }
    if (!extension.empty())
    {
        query += (query.empty() ? "?" : "&");
        query += "extension=" + UrlEncode(extension);
    }
    url += query;

    std::thread([url, on_ok, on_err]()
    {
        ix::HttpClient client;
        ix::HttpRequestArgsPtr args = MakeArgs(client, /*with_auth*/ true);

        auto resp = client.get(url, args);
        if (!IsSuccess(resp))
        {
            if (on_err)
            {
                on_err(MakeError(resp ? resp->statusCode : 0,
                                 resp ? resp->body : std::string(),
                                 resp ? resp->errorMsg : std::string("null response")));
            }
            return;
        }

        pxr::JsParseError perr;
        pxr::JsValue parsed = pxr::JsParseString(resp->body, &perr);
        std::vector<FileEntry> files;
        if (parsed.IsObject())
        {
            const pxr::JsObject& o = parsed.GetJsObject();
            auto it = o.find("files");
            if (it != o.end() && it->second.IsArray())
            {
                for (const pxr::JsValue& entry : it->second.GetJsArray())
                {
                    if (!entry.IsObject()) continue;
                    const pxr::JsObject& fo = entry.GetJsObject();
                    FileEntry fe;
                    fe.filepath  = JsGetString(fo, "filepath");
                    fe.filename  = JsGetString(fo, "filename");
                    fe.directory = JsGetString(fo, "directory");
                    fe.size      = JsGetInt(fo, "size");
                    fe.modified  = JsGetInt(fo, "modified");
                    files.push_back(std::move(fe));
                }
            }
        }
        if (on_ok) on_ok(files);
    }).detach();
}

void IdtxRestClient::create_session(const std::string& usd_file,
                                    const std::string& mode,
                                    SessionCallback on_ok,
                                    ErrorCallback on_err) const
{
    const std::string url = base_url_ + "/api/v1/sessions";

    pxr::JsObject body;
    body["usd_file"] = pxr::JsValue(usd_file);
    body["mode"] = pxr::JsValue(mode.empty() ? std::string("single_edit") : mode);
    const std::string payload = pxr::JsWriteToString(pxr::JsValue(body));

    std::thread([url, payload, on_ok, on_err]()
    {
        ix::HttpClient client;
        ix::HttpRequestArgsPtr args = MakeArgs(client, /*with_auth*/ true);
        args->extraHeaders["Content-Type"] = "application/json";

        auto resp = client.post(url, payload, args);
        if (!IsSuccess(resp))
        {
            if (on_err)
            {
                on_err(MakeError(resp ? resp->statusCode : 0,
                                 resp ? resp->body : std::string(),
                                 resp ? resp->errorMsg : std::string("null response")));
            }
            return;
        }

        pxr::JsParseError perr;
        pxr::JsValue parsed = pxr::JsParseString(resp->body, &perr);
        if (!parsed.IsObject())
        {
            if (on_err) on_err(MakeError(resp->statusCode, resp->body, "invalid json"));
            return;
        }
        const pxr::JsObject& o = parsed.GetJsObject();

        SessionInfo si;
        si.session_id   = JsGetString(o, "session_id");
        si.usd_file     = JsGetString(o, "usd_file");
        si.mode         = JsGetString(o, "mode");
        si.client_count = JsGetInt(o, "client_count");
        si.created_at   = JsGetInt(o, "created_at");
        si.ws_url       = JsGetString(o, "ws_url");
        si.protocol     = JsGetString(o, "protocol");

        if (si.session_id.empty())
        {
            if (on_err) on_err(MakeError(resp->statusCode, resp->body, "no session_id in response"));
            return;
        }
        if (on_ok) on_ok(si);
    }).detach();
}

void IdtxRestClient::delete_session(const std::string& session_id,
                                    DeleteCallback on_ok,
                                    ErrorCallback on_err) const
{
    const std::string url = base_url_ + "/api/v1/sessions/" + session_id;

    std::thread([url, on_ok, on_err]()
    {
        ix::HttpClient client;
        ix::HttpRequestArgsPtr args = MakeArgs(client, /*with_auth*/ true);

        auto resp = client.Delete(url, args);
        // 204 No Content on success; 404 means already gone — both are "ok".
        const int code = resp ? resp->statusCode : 0;
        if (IsSuccess(resp) || code == 404)
        {
            if (on_ok) on_ok();
            return;
        }
        if (on_err)
        {
            on_err(MakeError(code,
                             resp ? resp->body : std::string(),
                             resp ? resp->errorMsg : std::string("null response")));
        }
    }).detach();
}

} // namespace net
} // namespace idtxflow
