#include <idtxflow/net/adapters/transport/ix/IxHttpTransport.h>

#include <utility>

#include <ixwebsocket/IXHttpClient.h>

namespace idtxflow
{
namespace net
{
namespace adapters
{
namespace
{
    ix::HttpRequestArgsPtr make_args(ix::HttpClient& client,
                                     const ports::IHttpTransport::Request& request,
                                     int connect_ms,
                                     int transfer_ms)
    {
        ix::HttpRequestArgsPtr args = client.createRequest();
        args->connectTimeout = connect_ms / 1000;
        args->transferTimeout = transfer_ms / 1000;
        args->followRedirects = true;
        args->maxRedirects = 5;
        args->compress = false;
        args->extraHeaders["Accept"] = "application/json";
        for (const auto& [key, value] : request.headers)
        {
            args->extraHeaders[key] = value;
        }
        return args;
    }

    ports::IHttpTransport::Response to_response(const ix::HttpResponsePtr& r)
    {
        ports::IHttpTransport::Response out;
        if (!r)
        {
            out.status = 0;
            out.error = "null response";
            return out;
        }
        out.status = r->statusCode;
        out.body = r->body;
        out.error = r->errorMsg;
        for (const auto& [key, value] : r->headers)
        {
            out.headers[key] = value;
        }
        return out;
    }

    ports::IHttpTransport::Response do_request(const std::string& base_url,
                                               const ports::IHttpTransport::Request& request,
                                               int connect_ms,
                                               int transfer_ms)
    {
        ix::HttpClient client;
        ix::HttpRequestArgsPtr args = make_args(client, request, connect_ms, transfer_ms);

        const std::string url = !request.url.empty() ? request.url : (base_url + request.endpoint);

        ix::HttpResponsePtr resp;
        if (request.method == "POST")
        {
            resp = client.post(url, request.body, args);
        }
        else if (request.method == "DELETE")
        {
            resp = client.Delete(url, args);
        }
        else
        {
            resp = client.get(url, args);
        }
        return to_response(resp);
    }
} // namespace

void IxHttpTransport::set_base_url(std::string url)
{
    if (!url.empty() && url.back() == '/')
    {
        url.pop_back();
    }
    base_url_ = std::move(url);
}

std::string IxHttpTransport::base_url() const
{
    return base_url_;
}

void IxHttpTransport::set_timeouts(int connect_ms, int transfer_ms)
{
    connect_ms_ = connect_ms;
    transfer_ms_ = transfer_ms;
}

void IxHttpTransport::request_async(const Request& request, Cb callback)
{
    const std::string base = base_url_;
    const int connect_ms = connect_ms_;
    const int transfer_ms = transfer_ms_;
    pool_.submit([base, request, callback, connect_ms, transfer_ms]
    {
        Response response = do_request(base, request, connect_ms, transfer_ms);
        if (callback)
        {
            callback(response);
        }
    });
}

IxHttpTransport::Response IxHttpTransport::request_sync(const Request& request)
{
    return do_request(base_url_, request, connect_ms_, transfer_ms_);
}

} // namespace adapters
} // namespace net
} // namespace idtxflow
