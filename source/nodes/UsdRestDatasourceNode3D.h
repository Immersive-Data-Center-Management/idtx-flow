#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <idtxflow/exec/ExecBridgeManager.h>

#include <ixwebsocket/IXHttpClient.h>
#include <idtxflow/utils/Logger.h>
#include <idtxflow/exec/ExecBridgeHandler.h>
#include <idtxflow_godot/nodes/IUsdNode3D.h>

class UsdRestDatasourceNode3D : public godot::Node3D, public IExecBridgeHandler
                                , public IUsdNode3D
{
    GDCLASS(UsdRestDatasourceNode3D, Node3D)
    IUSDNODE(UsdRestDatasourceNode3D, true)
    
    IDTX_LOG_CATEGORY("UsdRestDatasourceNode3D")
    
public:
    /******************* Godot lifecycle hooks ***************************/
    void _process(double delta) override;
    void _enter_tree() override;
    void _exit_tree() override;
    
    /******************* ExecBridgeHandler ******************************/
    void OnComputeComplete(const std::vector<ExecComputeResult>& results) override;
    
    /******** Getter & Setter for Property De-/Serialization ***********/
    godot::String get_endpoint_uri() const;
    void set_endpoint_uri(const godot::String& endpoint_uri);
    godot::String get_query() const;
    void set_query(const godot::String& query);
    godot::String get_method() const;
    void set_method(const godot::String& method);
    godot::String get_json_body() const;
    void set_json_body(const godot::String& json_body);
    float get_refresh_interval() const;
    void set_refresh_interval(float refresh_interval);
    
protected:
    static void _bind_methods();
    
public:
    std::string endpoint_uri_;
    std::string query_;
    std::string method_ = "GET";
    std::string authorization_header_;
    std::string json_body_;
    float refresh_interval_ = 5.0f;
    double time_accumulator_ = 0.0;
    
private:
    /// TLS options passed to IXWebSocket's HttpClient.
    /// Default uses the system certificate store ("SYSTEM").
    ix::SocketTLSOptions tls_options_;
    ix::HttpClient http_client_ = ix::HttpClient(true); // use an async client
    // this allows us to ensure proper synchronization if the http callback would like to mutate the stage and
    // update data there. This should be queued into the ExecBridge to prevent data races.
    // openUSD does not allow concurrent stage mutations
    struct CallbackState
    {
        std::atomic<bool> accepting_results{true};
        std::atomic<bool> request_in_flight{false};
        std::atomic<uint64_t> generation{0};
        std::weak_ptr<idtxflow::exec::ExecBridge> bridge;
        pxr::SdfPath prim_path;
        pxr::TfToken attribute_name;
    };

    mutable std::mutex state_mutex_;
    std::shared_ptr<CallbackState> callback_state_ = std::make_shared<CallbackState>();
};
