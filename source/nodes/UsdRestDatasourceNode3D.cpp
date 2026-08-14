#include "UsdRestDatasourceNode3D.h"

#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/sdf/path.h>

#include <idtx/tokens.h>

#include <idtxflow_godot/nodes/UsdStageNode3D.h>

using namespace godot;

godot::String UsdRestDatasourceNode3D::get_endpoint_uri() const
{
    return endpoint_uri_.c_str();
}

void UsdRestDatasourceNode3D::set_endpoint_uri(const godot::String& endpoint_uri)
{
    endpoint_uri_ = endpoint_uri.utf8().get_data();
}

godot::String UsdRestDatasourceNode3D::get_query() const
{
    return query_.c_str();
}

void UsdRestDatasourceNode3D::set_query(const godot::String& query)
{
    query_ = query.utf8().get_data();
}

godot::String UsdRestDatasourceNode3D::get_method() const
{
    return method_.c_str();
}

void UsdRestDatasourceNode3D::set_method(const godot::String& method)
{
    method_ = method.utf8().get_data();
}

godot::String UsdRestDatasourceNode3D::get_json_body() const
{
    return json_body_.c_str();
}

void UsdRestDatasourceNode3D::set_json_body(const godot::String& json_body)
{
    json_body_ = json_body.utf8().get_data();
}

float UsdRestDatasourceNode3D::get_refresh_interval() const
{
    return refresh_interval_;
}

void UsdRestDatasourceNode3D::set_refresh_interval(float refresh_interval)
{
    refresh_interval_ = refresh_interval;
}

void UsdRestDatasourceNode3D::_process(double delta)
{
    Node3D::_process(delta);
    
    time_accumulator_ += delta;
    if (time_accumulator_ >= static_cast<double>(refresh_interval_))
    {
        time_accumulator_ = 0.0;
        
        const std::string& url = endpoint_uri_ + "?" + query_;
        
        http_client_.setTLSOptions(tls_options_);
        ix::HttpRequestArgsPtr args = http_client_.createRequest(url, method_);
        args->followRedirects = true;
        args->maxRedirects = 5;
        args->connectTimeout = 30;
        args->transferTimeout = 120;
        args->compress = false;
        args->body = json_body_;
        args->extraHeaders.insert({"Authorization", authorization_header_});
        
        if (!http_client_.performRequest(args, [&, url](const ix::HttpResponsePtr& response)
        {
            // ensure the responses of the async requests are not stepping on each others toes
            std::scoped_lock<std::mutex> lock(mutex_);
            
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
        }))
        {
            IDTX_LOG(IDTX_ERROR, "Unable to perform the async request for '{}'", url);
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
    
    // bind method for property serialization/deserialization
    ClassDB::bind_method(D_METHOD("set_endpoint_uri", "p_uri"), &UsdRestDatasourceNode3D::set_endpoint_uri);
    ClassDB::bind_method(D_METHOD("get_endpoint_uri"), &UsdRestDatasourceNode3D::get_endpoint_uri);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "endpoint_uri_",
            PROPERTY_HINT_NONE, "" , PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_READ_ONLY ),
        "set_endpoint_uri", "get_endpoint_uri");
    
    ClassDB::bind_method(D_METHOD("set_query", "p_query"), &UsdRestDatasourceNode3D::set_query);
    ClassDB::bind_method(D_METHOD("get_query"), &UsdRestDatasourceNode3D::get_query);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "query_",
            PROPERTY_HINT_NONE, "" , PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_READ_ONLY ),
        "set_query", "get_query");
    
    ClassDB::bind_method(D_METHOD("set_method", "p_method"), &UsdRestDatasourceNode3D::set_method);
    ClassDB::bind_method(D_METHOD("get_method"), &UsdRestDatasourceNode3D::get_method);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "method_",
            PROPERTY_HINT_NONE, "" , PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_READ_ONLY ),
        "set_method", "get_method");
    
    ClassDB::bind_method(D_METHOD("set_json_body", "p_json_body"), &UsdRestDatasourceNode3D::set_json_body);
    ClassDB::bind_method(D_METHOD("get_json_body"), &UsdRestDatasourceNode3D::get_json_body);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "json_body_",
            PROPERTY_HINT_NONE, "" , PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_READ_ONLY ),
        "set_json_body", "get_json_body");
    
    ClassDB::bind_method(D_METHOD("set_refresh_interval", "p_interval"), &UsdRestDatasourceNode3D::set_refresh_interval);
    ClassDB::bind_method(D_METHOD("get_refresh_interval"), &UsdRestDatasourceNode3D::get_refresh_interval);
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "refresh_interval",
            PROPERTY_HINT_NONE, "" , PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_READ_ONLY ),
        "set_refresh_interval", "get_refresh_interval");
}
