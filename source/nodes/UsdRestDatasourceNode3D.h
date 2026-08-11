#pragma once

#include <godot_cpp/classes/node3d.hpp>

#include <ixwebsocket/IXHttpClient.h>
#include <idtxflow/utils/Logger.h>
#include <idtxflow/exec/ExecBridgeHandler.h>
#include <idtxflow_godot/nodes/IUsdNode3D.h>

class UsdRestDatasourceNode3D : public godot::Node3D, public IExecBridgeHandler
                                , public IUsdNode3D
{
    GDCLASS(UsdRestDatasourceNode3D, Node3D)
    IUSDNODE(UsdRestDatasourceNode3D)
    
    IDTX_LOG_CATEGORY("UsdRestDatasourceNode3D")
    
public:
    /******************* Godot lifecycle hooks ***************************/
    void _process(double delta) override;
    void _enter_tree() override;
    
    /******************* ExecBridgeHandler ******************************/
    void OnComputeComplete(const std::vector<ExecComputeResult>& results) override;
    
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
    mutable std::mutex mutex_;
};
