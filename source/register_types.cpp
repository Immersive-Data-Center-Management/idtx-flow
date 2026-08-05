#include "register_types.h"

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#include <idtxflow/converter/MdlMaterialConverter.h>
#include <idtxflow/resolver/HttpResolver.h>
#include <idtxflow_godot/nodes/UsdStageNode3D.h>
#include <idtxflow/exec/ExecBridgeManager.h>

#include <idtx/EnvironmentProvider.h>

#include "nodes/UsdStaticBodyNode3D.h"
#include "nodes/UsdMeshInstanceNode3D.h"
#include "nodes/UsdMockDatasourceFloatNode3D.h"
#include "nodes/UsdMultiMeshInstanceNode3D.h"
#include "nodes/UsdRestDatasourceNode3D.h"
#include "nodes/UsdXFormNode3D.h"
#include "utils/IDTXFlowGodotLogger.h"
#include "utils/IDTXFlowProjectSettings.h"
#include "exec/GodotEnvironmentProviders.h"

using namespace godot;

// Static logger instance — lives for the lifetime of this dll
static idtxflow::utils::IDTXFlowGodotLogger g_logger;

// Static environment provider instances — live for the lifetime of this dll. They implement
// the host-agnostic idtx::IEnvironmentProvider contract and are registered with the USD
// library's EnvironmentProviderRegistry so the Compute_Environment node can resolve its
// string input via these host-side implementations.
//  - g_env_var_provider   handles keys prefixed "env:"     (process environment variables)
//  - g_project_setting_provider (constructed lazily on the main thread at init) handles keys
//    prefixed "project:" (Godot ProjectSettings snapshot)
static idtxflow::exec::GodotEnvVarProvider g_env_var_provider;
static idtxflow::exec::GodotAuthProvider g_auth_provider;
static idtxflow::exec::GodotProjectSettingProvider* g_project_setting_provider = nullptr;

#ifdef IDTXFLOW_MDL_ENABLED
#include <idtxflow/converter/MdlMaterialConverter.h>

inline std::string get_gdextension_dir()
{
#ifdef MI_PLATFORM_WINDOWS
    char buffer[MAX_PATH];
    HMODULE hm = nullptr;
    // Get handle of the current DLL (this GDExtension)
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&get_gdextension_dir,
        &hm
    );
    GetModuleFileNameA(hm, buffer, MAX_PATH);
    std::string path(buffer);
    return path.substr(0, path.find_last_of("\\/"));
#else
    Dl_info info;
    if (dladdr((void*)&get_gdextension_dir, &info) == 0)
    {
        return "";
    }
    std::string path(info.dli_fname);
    return path.substr(0, path.find_last_of("/"));
#endif
}
#endif

void initialize_idtxflow_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    // Initialize logger
    idtxflow::utils::Log::set_logger(&g_logger);
    
    GDREGISTER_CLASS(UsdStageNode3D)
    GDREGISTER_CLASS(UsdXformNode3D)
    GDREGISTER_CLASS(UsdMeshInstanceNode3D)
    GDREGISTER_CLASS(UsdMultiMeshInstanceNode3D)
    GDREGISTER_CLASS(UsdSkeletonNode3D)
    GDREGISTER_CLASS(UsdMockDatasourceFloatNode3D)
    GDREGISTER_CLASS(UsdRestDatasourceNode3D)
    GDREGISTER_CLASS(UsdStaticBodyNode3D)
    
#ifdef IDTXFLOW_MDL_ENABLED
    // activate the mdl material conversion
    std::string extension_dir = get_gdextension_dir();
    std::vector<std::string> additionalModulPaths;
    if (ProjectSettings *project_settings = godot::ProjectSettings::get_singleton())
    {
        // ensure that the projects resource and user directories can be used as mdl module search paths
        additionalModulPaths.emplace_back(project_settings->globalize_path("res://").utf8().get_data());
        additionalModulPaths.emplace_back(project_settings->globalize_path("user://").utf8().get_data());
    }
    idtxflow::converter::StartupMdlMaterialConverter(extension_dir, additionalModulPaths);
#endif
    
    // Register the add-on specific project settings (OIDC / OAuth) into the editor's
    // Project Settings dialog. This must run on the main thread (we are on it here) since
    // it touches the ProjectSettings singleton. Secrets (password, client secret) are
    // flagged internal so they are never serialized to project.godot.
    idtxflow::utils::IDTXFlowProjectSettings::Register();

    // Configure the HTTP asset resolver with the default IXWebSocket-based fetcher
    pxr::UsdHttpAssetResolver::Configure(
        ProjectSettings::get_singleton()->globalize_path("user://usd_cache").utf8().get_data());

    // Register the host-side environment providers with the USD library's registry BEFORE the
    // exec worker thread starts, so the Compute_Environment node can resolve values from the
    // very first computation cycle. The provider objects are host-owned and live for the DLL
    // lifetime; the USD library only stores the pointers.
    //
    // The project-settings provider snapshots ProjectSettings here on the MAIN thread (its
    // constructor reads the ProjectSettings singleton) so the worker thread later reads only
    // from that immutable snapshot.
    g_project_setting_provider = new idtxflow::exec::GodotProjectSettingProvider();
    idtx::EnvironmentProviderRegistry::Instance().RegisterProvider("env", &g_env_var_provider);
    idtx::EnvironmentProviderRegistry::Instance().RegisterProvider("auth", &g_auth_provider);
    idtx::EnvironmentProviderRegistry::Instance().RegisterProvider("project", g_project_setting_provider);

    // Run the openExec computation bridge
    idtxflow::exec::ExecBridgeManager::Instance().Start();
    
    IDTX_LOGF(IDTX_INFO, "GDExtension initialized");
}

void uninitialize_idtxflow_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    
    // Stop the openExec computation bridge
    idtxflow::exec::ExecBridgeManager::Instance().Cancel();

    // Unregister the host-side environment providers only AFTER the exec worker thread has
    // been cancelled above. This guarantees no in-flight computation can dereference a
    // provider pointer while / after it is being removed. The provider objects are host-owned;
    // the env-var provider is a static and needs no deletion, the project-settings provider
    // was heap-allocated at init and is freed here.
    idtx::EnvironmentProviderRegistry::Instance().UnregisterAll();
    delete g_project_setting_provider;
    g_project_setting_provider = nullptr;

#ifdef IDTXFLOW_MDL_ENABLED
    // shutdown the mdl material conversion
    idtxflow::converter::ShutdownMdlMaterialConverter();
#endif
    
    IDTX_LOGF(IDTX_INFO, "GDExtension uninitialized");
    
    // Clear logger reference
    idtxflow::utils::Log::set_logger(nullptr);
}

extern "C" {
    GDExtensionBool GDE_EXPORT idtxflow_library_init(
        GDExtensionInterfaceGetProcAddress p_get_proc_address,
        const GDExtensionClassLibraryPtr p_library,
        GDExtensionInitialization *r_initialization) {
        
        GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

        init_obj.register_initializer(initialize_idtxflow_module);
        init_obj.register_terminator(uninitialize_idtxflow_module);
        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

        return init_obj.init();
    }
}
