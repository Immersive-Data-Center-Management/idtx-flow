#include "UsdRestDatasourceNode3D.h"

#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/sdf/path.h>

#include <idtx/tokens.h>

#include <idtxflow_godot/nodes/UsdStageNode3D.h>

void UsdRestDatasourceNode3D::_process(double delta)
{
    Node3D::_process(delta);
    
    time_accumulator_ += delta;
    if (time_accumulator_ >= static_cast<double>(refresh_interval_))
    {
        time_accumulator_ = 0.0;
        
        const std::string& url = endpoint_uri_ + "?" + query_;
        
        http_client_.setTLSOptions(tls_options_);
        ix::HttpRequestArgsPtr args = http_client_.createRequest(url);
        args->followRedirects = true;
        args->maxRedirects = 5;
        args->connectTimeout = 30;
        args->transferTimeout = 120;
        args->compress = false;
        args->extraHeaders.insert({"Authorization", authorization_header_});
        
        ix::HttpResponsePtr response;
        
        if (method_ == "POST")
        {
            response = http_client_.post(url, json_body_, args);
        } else if (method_ == "GET")
        {
            response = http_client_.get(url, args);
        } else
        {
            return;
        }
        
        if (!response || response->statusCode < 200 || response->statusCode >= 300)
        {
            const std::string err = response ? response->errorMsg : "null response";
            const std::string body = response ? response->body : "null";
            const int code = response ? response->statusCode : 0;
            IDTX_LOG(IDTX_ERROR, "Request failed for '{}': err: {} - body: {} (HTTP {})", url, err, body, code);
            return;
        }
        std::string data = response->body;
        IDTX_LOG(IDTX_DEBUG, "Set new data value to: {}", data);
        if (pxr::UsdAttribute attribute = stage_node_->get_stage()->GetPrimAtPath(pxr::SdfPath(prim_path_.utf8().get_data()))
                .GetAttribute(pxr::IDTXTokens->outputsData))
        {
            if (!attribute.Set(data.c_str()))
                IDTX_LOG(IDTX_ERROR,"unable to set data input value for json at '{}'", prim_path_.utf8().get_data());
        }
    }
}

void UsdRestDatasourceNode3D::_enter_tree()
{
    Node3D::_enter_tree();
}

void UsdRestDatasourceNode3D::OnComputeComplete(const std::vector<ExecComputeResult>& results)
{
    for (const auto& result : results)
    {
        if (result.primAttribute == "authorization" && result.value.IsHolding<std::string>())
        {
            authorization_header_ = result.value.Get<std::string>();
        }
        
        if (result.primAttribute == "jsonBody" && result.value.IsHolding<std::string>())
        {
            json_body_ = result.value.Get<std::string>();
        }
    }
}

void UsdRestDatasourceNode3D::_bind_methods()
{
    // bind methods from the inherited IUsdNode3D interface
    IUSDNODE_IMPLEMENT_BINDINGS(UsdRestDatasourceNode3D)
}
