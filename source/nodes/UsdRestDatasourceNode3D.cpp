#include "UsdRestDatasourceNode3D.h"

#include <idtx/tokens.h>
#include <idtxflow_godot/nodes/UsdStageNode3D.h>

using namespace godot;

String UsdRestDatasourceNode3D::get_endpoint_uri() const
{
    std::lock_guard<std::mutex> lock(state_mutex_); return endpoint_uri_.c_str();
}

void UsdRestDatasourceNode3D::set_endpoint_uri(const String& value)
{
    std::lock_guard<std::mutex> lock(state_mutex_); endpoint_uri_ = value.utf8().get_data();
}

String UsdRestDatasourceNode3D::get_query() const
{
    std::lock_guard<std::mutex> lock(state_mutex_); return query_.c_str();
}

void UsdRestDatasourceNode3D::set_query(const String& value)
{
    std::lock_guard<std::mutex> lock(state_mutex_); query_ = value.utf8().get_data();
}

String UsdRestDatasourceNode3D::get_method() const
{
    std::lock_guard<std::mutex> lock(state_mutex_); return method_.c_str();
}

void UsdRestDatasourceNode3D::set_method(const String& value)
{
    std::lock_guard<std::mutex> lock(state_mutex_); method_ = value.utf8().get_data();
}

String UsdRestDatasourceNode3D::get_json_body() const
{
    std::lock_guard<std::mutex> lock(state_mutex_); return json_body_.c_str();
}

void UsdRestDatasourceNode3D::set_json_body(const String& value)
{
    std::lock_guard<std::mutex> lock(state_mutex_); json_body_ = value.utf8().get_data();
}

float UsdRestDatasourceNode3D::get_refresh_interval() const
{
    std::lock_guard<std::mutex> lock(state_mutex_); return refresh_interval_;
}

void UsdRestDatasourceNode3D::set_refresh_interval(float value)
{
    std::lock_guard<std::mutex> lock(state_mutex_); refresh_interval_ = value;
}

void UsdRestDatasourceNode3D::_enter_tree()
{
    Node3D::_enter_tree();
    auto state = callback_state_;
    state->accepting_results.store(true);
    if (stage_node_ && stage_node_->get_stage()) {
        state->prim_path = pxr::SdfPath(prim_path_.utf8().get_data());
        state->attribute_name = pxr::IDTXTokens->outputsData;
        state->bridge = idtxflow::exec::ExecBridgeManager::Instance().FindExecBridgeForStage(stage_node_->get_stage());
    }
}

void UsdRestDatasourceNode3D::_exit_tree()
{
    auto state = callback_state_;
    state->accepting_results.store(false);
    state->generation.fetch_add(1);
    state->bridge.reset();
    Node3D::_exit_tree();
}

void UsdRestDatasourceNode3D::_process(double delta)
{
    Node3D::_process(delta);
    std::string endpoint, query, method, body, authorization;
    float interval;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        time_accumulator_ += delta;
        interval = refresh_interval_;
        
        if (time_accumulator_ < interval || callback_state_->request_in_flight.load()) return;
        
        time_accumulator_ = 0.0;
        endpoint = endpoint_uri_;
        query = query_;
        method = method_;
        body = json_body_;
        authorization = authorization_header_;
    }
    auto state = callback_state_;
    if (!state->accepting_results.load() || state->bridge.expired()) return;
    
    state->request_in_flight.store(true);
    const uint64_t generation = state->generation.load();
    const std::string url = endpoint + (query.empty() ? "" : "?" + query);
    
    http_client_.setTLSOptions(tls_options_);
    ix::HttpRequestArgsPtr args = http_client_.createRequest(url, method);
    args->followRedirects = true;
    args->maxRedirects = 5;
    args->connectTimeout = 30;
    args->transferTimeout = 120; 
    args->compress = false;
    args->body = body;
    
    if (!authorization.empty()) args->extraHeaders.insert({"Authorization", authorization});
    if (!http_client_.performRequest(args, [state, generation, url](const ix::HttpResponsePtr& response) {
        state->request_in_flight.store(false);
        if (!state->accepting_results.load() || state->generation.load() != generation) return;
        
        if (!response || response->statusCode < 200 || response->statusCode >= 300)
        {
            IDTX_LOG(IDTX_WARN, "REST request failed for '{}' (HTTP {})", url, response ? response->statusCode : 0); return;
        }
        auto bridge = state->bridge.lock();
        if (!bridge) return;
        
        idtxflow::exec::StageAttributeUpdate update{
            state->prim_path, 
            state->attribute_name,
            pxr::VtValue(std::string(response->body)), 
            generation, 
            "rest"
        };
        if (!bridge->EnqueueAttributeUpdate(std::move(update))) 
            IDTX_LOG(IDTX_DEBUG, "REST update rejected for '{}'", url);
    }))
    {
        state->request_in_flight.store(false); IDTX_LOG(IDTX_ERROR, "Unable to perform async request for '{}'", url);
    }
}

void UsdRestDatasourceNode3D::OnComputeComplete(const std::vector<ExecComputeResult>& results)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (const auto& result : results) {
        if (result.primAttribute == "authorization" && result.value.IsHolding<std::string>()) authorization_header_ = result.value.Get<std::string>();
        if (result.primAttribute == "jsonBody" && result.value.IsHolding<std::string>()) json_body_ = result.value.Get<std::string>();
    }
}

void UsdRestDatasourceNode3D::_bind_methods()
{
    IUSDNODE_IMPLEMENT_BINDINGS(UsdRestDatasourceNode3D)
    ClassDB::bind_method(D_METHOD("set_endpoint_uri", "p_uri"), &UsdRestDatasourceNode3D::set_endpoint_uri);
    ClassDB::bind_method(D_METHOD("get_endpoint_uri"), &UsdRestDatasourceNode3D::get_endpoint_uri);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "endpoint_uri_", 
            PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_READ_ONLY),
            "set_endpoint_uri", "get_endpoint_uri");
    
    ClassDB::bind_method(D_METHOD("set_query", "p_query"), &UsdRestDatasourceNode3D::set_query);
    ClassDB::bind_method(D_METHOD("get_query"), &UsdRestDatasourceNode3D::get_query);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "query_",
            PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_READ_ONLY),
            "set_query", "get_query");
    
    ClassDB::bind_method(D_METHOD("set_method", "p_method"), &UsdRestDatasourceNode3D::set_method);
    ClassDB::bind_method(D_METHOD("get_method"), &UsdRestDatasourceNode3D::get_method);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "method_",
            PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_READ_ONLY),
            "set_method", "get_method");
    
    ClassDB::bind_method(D_METHOD("set_json_body", "p_json_body"), &UsdRestDatasourceNode3D::set_json_body);
    ClassDB::bind_method(D_METHOD("get_json_body"), &UsdRestDatasourceNode3D::get_json_body);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "json_body_",
            PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_READ_ONLY),
            "set_json_body", "get_json_body");
    
    ClassDB::bind_method(D_METHOD("set_refresh_interval", "p_interval"), &UsdRestDatasourceNode3D::set_refresh_interval);
    ClassDB::bind_method(D_METHOD("get_refresh_interval"), &UsdRestDatasourceNode3D::get_refresh_interval);
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "refresh_interval",
            PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_READ_ONLY),
            "set_refresh_interval", "get_refresh_interval");
}