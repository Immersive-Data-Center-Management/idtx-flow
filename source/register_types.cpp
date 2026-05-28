#include "register_types.h"

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

#ifdef __ANDROID__
#include <android/log.h>
#endif

#include <pxr/base/plug/registry.h>

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

#ifdef __ANDROID__
// ---------------------------------------------------------------------------
// Android-only: register USD plugin metadata from the APK's res:// directory.
//
// In a monolithic libusd_ms.so build the plugInfo.json files are not
// auto-discovered by PlugRegistry.  This causes ArGetResolver() to call
// DemandPluginForType(ArDefaultResolver) → plugin not found → abort().
//
// Fix: copy the android/usd/ plugInfo tree from res:// (PCK) to the real
// filesystem (user://) and call PlugRegistry::RegisterPlugins() once.
// ---------------------------------------------------------------------------
static void _copy_res_to_user(const String& src, const String& dst)
{
    // Copy a single file from res:// to the real filesystem
    Ref<FileAccess> fsrc = FileAccess::open(src, FileAccess::READ);
    if (!fsrc.is_valid()) return;
    DirAccess::make_dir_recursive_absolute(
        ProjectSettings::get_singleton()->globalize_path(dst.get_base_dir()));
    Ref<FileAccess> fdst = FileAccess::open(dst, FileAccess::WRITE);
    if (!fdst.is_valid()) return;
    fdst->store_buffer(fsrc->get_buffer(fsrc->get_length()));
}

static void _extract_res_dir(const String& res_dir, const String& dst_dir)
{
    // Recursively copy all files from a res:// directory to user://
    Ref<DirAccess> dir = DirAccess::open(res_dir);
    if (!dir.is_valid()) return;
    dir->list_dir_begin();
    String entry = dir->get_next();
    while (!entry.is_empty()) {
        if (entry != String(".") && entry != String("..")) {
            String src_path = res_dir.path_join(entry);
            String dst_path = dst_dir.path_join(entry);
            if (dir->current_is_dir()) {
                _extract_res_dir(src_path, dst_path);
            } else {
                _copy_res_to_user(src_path, dst_path);
            }
        }
        entry = dir->get_next();
    }
    dir->list_dir_end();
}

static void _register_usd_plugins_android()
{
    __android_log_print(ANDROID_LOG_INFO, "IDTXFlow_Plug", "ENTER _register_usd_plugins_android");

    // --- Built-in OpenUSD plugins (ar, sdf, usdGeom, etc.) ---
    const String usd_src = String("res://addons/IDTXFlow/bin/android/usd");
    const String usd_dst = String("user://usd");
    const String done_marker = usd_dst + String("/.extracted");

    if (!FileAccess::file_exists(done_marker)) {
        __android_log_print(ANDROID_LOG_INFO, "IDTXFlow_Plug", "Extracting USD plugin metadata...");
        _extract_res_dir(usd_src, usd_dst);
        Ref<FileAccess> marker = FileAccess::open(done_marker, FileAccess::WRITE);
        if (marker.is_valid()) marker->store_string(String("ok"));
        __android_log_print(ANDROID_LOG_INFO, "IDTXFlow_Plug", "USD plugin metadata extracted");
    } else {
        __android_log_print(ANDROID_LOG_INFO, "IDTXFlow_Plug", "USD plugin metadata already extracted (marker exists)");
    }

    String usd_real = ProjectSettings::get_singleton()->globalize_path(usd_dst);
    __android_log_print(ANDROID_LOG_INFO, "IDTXFlow_Plug", "Calling RegisterPlugins (usd): %s", usd_real.utf8().get_data());
    pxr::PlugRegistry::GetInstance().RegisterPlugins(usd_real.utf8().get_data());

    // --- Custom schema plugins (idtx: IDTXCollisionAPI etc., godot resolver) ---
    // These live in bin/plugin/ and are NOT inside the OpenUSD install tree.
    // Without registering them, PlugRegistry cannot find our custom prim types
    // and any USD stage containing IDTXCollisionAPI/IDTXCollisionSetAPI/IDTXInteractionAPI
    // prims will silently fall back to UsdSchemaBase — attributes won't resolve.
    const String plugin_src = String("res://addons/IDTXFlow/bin/plugin/usd");
    const String plugin_dst = String("user://usd_plugin");
    const String plugin_marker = plugin_dst + String("/.extracted");

    if (!FileAccess::file_exists(plugin_marker)) {
        __android_log_print(ANDROID_LOG_INFO, "IDTXFlow_Plug", "Extracting custom schema plugins...");
        _extract_res_dir(plugin_src, plugin_dst);
        Ref<FileAccess> marker = FileAccess::open(plugin_marker, FileAccess::WRITE);
        if (marker.is_valid()) marker->store_string(String("ok"));
        __android_log_print(ANDROID_LOG_INFO, "IDTXFlow_Plug", "Custom schema plugins extracted");
    } else {
        __android_log_print(ANDROID_LOG_INFO, "IDTXFlow_Plug", "Custom schema plugins already extracted (marker exists)");
    }

    String plugin_real = ProjectSettings::get_singleton()->globalize_path(plugin_dst);
    __android_log_print(ANDROID_LOG_INFO, "IDTXFlow_Plug", "Calling RegisterPlugins (custom): %s", plugin_real.utf8().get_data());
    pxr::PlugRegistry::GetInstance().RegisterPlugins(plugin_real.utf8().get_data());

    __android_log_print(ANDROID_LOG_INFO, "IDTXFlow_Plug", "RegisterPlugins done");
}
#endif // __ANDROID__

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

#ifdef __ANDROID__
    // Register USD plugin metadata so PlugRegistry can find ArDefaultResolver
    // before the first ArGetResolver() call.
    _register_usd_plugins_android();
#endif
    
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
