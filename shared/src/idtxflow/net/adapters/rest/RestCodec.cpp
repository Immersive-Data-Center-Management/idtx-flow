#include <idtxflow/net/adapters/rest/RestCodec.h>

#include <string>

#include <pxr/base/js/json.h>
#include <pxr/base/js/value.h>

namespace idtxflow
{
namespace net
{
namespace adapters
{
namespace
{
    std::string js_get_string(const pxr::JsObject& obj, const char* key)
    {
        auto it = obj.find(key);
        if (it != obj.end() && it->second.IsString())
        {
            return it->second.GetString();
        }
        return {};
    }

    int64_t js_get_int(const pxr::JsObject& obj, const char* key)
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

    // Backend listings may use Windows separators; the session and download
    // contract expects forward slashes. `strip_leading` also removes a single
    // leading slash so a path is relative, matching what the contract keys on.
    std::string normalize_path(std::string path, bool strip_leading)
    {
        for (char& c : path)
        {
            if (c == '\\') c = '/';
        }
        if (strip_leading && !path.empty() && path.front() == '/')
        {
            path.erase(path.begin());
        }
        return path;
    }

    // The backend `modified` is an implementation-defined file-time count whose
    // unit is not guaranteed. Collapse plainly-too-large values toward seconds and
    // accept any result from about a day past the Unix epoch onward, so genuine
    // early timestamps still resolve; report 0 for degenerate near-zero counts
    // that cannot be read as a time.
    int64_t decode_modified_epoch(int64_t raw)
    {
        if (raw <= 0)
        {
            return 0;
        }
        int64_t secs = raw;
        while (secs > 10000000000)   // beyond a plausible seconds range (~year 2286)
        {
            secs /= 1000;
        }
        if (secs >= 86400 && secs <= 10000000000)
        {
            return secs;
        }
        return 0;
    }
} // namespace

bool RestCodec::parse_login(const std::string& body, model::LoginResult& out)
{
    pxr::JsParseError perr;
    pxr::JsValue parsed = pxr::JsParseString(body, &perr);
    if (!parsed.IsObject())
    {
        IDTX_LOG(IDTX_WARN, "parse_login: response body is not a JSON object");
        return false;
    }
    const pxr::JsObject& o = parsed.GetJsObject();

    out.access_token  = js_get_string(o, "access_token");
    out.token_type    = js_get_string(o, "token_type");
    if (out.token_type.empty()) out.token_type = "Bearer";
    out.expires_in    = js_get_int(o, "expires_in");
    out.refresh_token = js_get_string(o, "refresh_token");
    out.scope         = js_get_string(o, "scope");

    return !out.access_token.empty();
}

bool RestCodec::parse_files(const std::string& body, std::vector<model::FileEntry>& out)
{
    pxr::JsParseError perr;
    pxr::JsValue parsed = pxr::JsParseString(body, &perr);
    if (!parsed.IsObject())
    {
        IDTX_LOG(IDTX_WARN, "parse_files: response body is not a JSON object");
        return false;
    }
    const pxr::JsObject& o = parsed.GetJsObject();
    auto it = o.find("files");
    if (it == o.end() || !it->second.IsArray())
    {
        return true;   // valid response with no files
    }

    for (const pxr::JsValue& entry : it->second.GetJsArray())
    {
        if (!entry.IsObject()) continue;
        const pxr::JsObject& fo = entry.GetJsObject();
        model::FileEntry fe;
        fe.filepath       = normalize_path(js_get_string(fo, "filepath"), true);
        fe.filename       = js_get_string(fo, "filename");
        fe.directory      = normalize_path(js_get_string(fo, "directory"), false);
        fe.size           = js_get_int(fo, "size");
        fe.modified       = js_get_int(fo, "modified");
        fe.modified_epoch = decode_modified_epoch(fe.modified);
        out.push_back(std::move(fe));
    }
    return true;
}

bool RestCodec::parse_session(const std::string& body, model::SessionInfo& out)
{
    pxr::JsParseError perr;
    pxr::JsValue parsed = pxr::JsParseString(body, &perr);
    if (!parsed.IsObject())
    {
        IDTX_LOG(IDTX_WARN, "parse_session: response body is not a JSON object");
        return false;
    }
    const pxr::JsObject& o = parsed.GetJsObject();

    out.session_id   = js_get_string(o, "session_id");
    out.usd_file     = js_get_string(o, "usd_file");
    out.mode         = js_get_string(o, "mode");
    out.client_count = js_get_int(o, "client_count");
    out.created_at   = js_get_int(o, "created_at");
    out.ws_url       = js_get_string(o, "ws_url");
    out.protocol     = js_get_string(o, "protocol");

    return !out.session_id.empty();
}

model::RestError RestCodec::parse_error(int http_code,
                                        const std::string& body,
                                        const std::string& transport_err)
{
    model::RestError e;
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
        e.error_code = js_get_string(o, "error");
        e.message = js_get_string(o, "message");
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

std::string RestCodec::make_login_body(const std::string& username,
                                       const std::string& password)
{
    pxr::JsObject body;
    body["username"] = pxr::JsValue(username);
    body["password"] = pxr::JsValue(password);
    return pxr::JsWriteToString(pxr::JsValue(body));
}

std::string RestCodec::make_session_body(const std::string& usd_file,
                                         const std::string& mode)
{
    pxr::JsObject body;
    body["usd_file"] = pxr::JsValue(usd_file);
    body["mode"] = pxr::JsValue(mode.empty() ? std::string("single_edit") : mode);
    return pxr::JsWriteToString(pxr::JsValue(body));
}

} // namespace adapters
} // namespace net
} // namespace idtxflow
