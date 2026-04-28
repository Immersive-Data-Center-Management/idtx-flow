#include "register_types.h"

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#include <idtxflow/converter/MdlMaterialConverter.h>
#include <idtxflow/resolver/HttpResolver.h>
#include <idtxflow_godot/nodes/UsdStageNode3D.h>

#include "nodes/UsdStaticBodyNode3D.h"
#include "nodes/UsdMeshInstanceNode3D.h"
#include "nodes/UsdMultiMeshInstanceNode3D.h"
#include "nodes/UsdXFormNode3D.h"
#include "utils/IDTXFlowGodotLogger.h"

using namespace godot;

// Static logger instance — lives for the lifetime of this dll
static idtxflow::utils::IDTXFlowGodotLogger g_logger;

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
    
    // Configure the HTTP asset resolver with the default IXWebSocket-based fetcher
    pxr::UsdHttpAssetResolver::Configure(
        ProjectSettings::get_singleton()->globalize_path("user://usd_cache").utf8().get_data());
    
    IDTX_LOGF(IDTX_INFO, "GDExtension initialized");
}

void uninitialize_idtxflow_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    
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
