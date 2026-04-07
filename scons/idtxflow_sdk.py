"""
SCons tool: idtxflow_sdk
Composes the IDTXFlow Godot SDK by copying built artifacts (libraries and headers)
into a single SDK output directory.

This step requires the GDExtension (and it's dependencies) and the bootstrap lib to
be build with the respective SCons tools.

Usage in SConstruct:
    env.ComposeIdtxFlowGodotSDK()
"""
import os
import pathlib
import shutil


def generate(env):
    env.AddMethod(_compose_idtxflow_sdk, 'ComposeIdtxFlowGodotSDK')


def exists(env):
    return True


def _compose_idtxflow_sdk(env):
    """Register an SDK composition step that depends on the built libraries."""
    sdk_root = "build/idtxflow-sdk"
    stamp_file = f"{sdk_root}/.sdk_stamp"

    # Use env.Command to register a build-phase action with a stamp file target.
    stamp = env.Command(
        target=stamp_file,
        source=[],
        action=_do_compose_sdk,
    )

    # Declare explicit dependencies on the library nodes so SCons builds
    # them before attempting the SDK composition.
    if 'gdextension_library_node' in env:
        env.Depends(stamp, env['gdextension_library_node'])
    if 'ext_bootstrap_library_node' in env:
        env.Depends(stamp, env['ext_bootstrap_library_node'])

    # Always rebuild so that header changes are picked up.
    env.AlwaysBuild(stamp)
    env.Default(stamp)

    return stamp


def _do_compose_sdk(target, source, env):
    """Action callback executed during the build phase."""
    print("Composing IDTXFlow SDK Artifacts")

    platform_name = env["platform_name"]

    sdk_root = "build/idtxflow-sdk"
    sdk_includes = f"{sdk_root}/include"
    sdk_libs = f"{sdk_root}/lib"

    open_usd_version = env.get('openusd_version', '')
    is_android = env.get('is_android', False)
    if is_android:
        open_usd_path = f"thirdparty/openusd-{open_usd_version}-android"
    else:
        open_usd_path = f"thirdparty/openusd-{open_usd_version}"

    # Ensure target directories exist
    os.makedirs(sdk_libs, exist_ok=True)
    os.makedirs(sdk_includes, exist_ok=True)

    # Copy the lib/so files into the SDK folder that will be required to link against   
    if platform_name == "windows":
        shutil.copy(f"{env['gdextension_lib_dir']}/{env['gdextension_lib']}.lib", f"{sdk_libs}")
        shutil.copy(f"{env['ext_bootstrap_lib_dir']}/{env['ext_bootstrap_lib']}.lib",f"{sdk_libs}")
        shutil.copy(f"{open_usd_path}/lib/usd_ms.lib", f"{sdk_libs}")
        shutil.copy(f"{open_usd_path}/lib/tbb12.lib", f"{sdk_libs}")
    elif platform_name == "macos":
        shutil.copy(f"{env['gdextension_lib_dir']}/{env['gdextension_lib']}.dylib", f"{sdk_libs}")
        shutil.copy(f"{env['ext_bootstrap_lib_dir']}/{env['ext_bootstrap_lib']}.a",f"{sdk_libs}")
        shutil.copy(f"{open_usd_path}/lib/libusd_ms.dylib", f"{sdk_libs}")
        shutil.copy(f"{open_usd_path}/lib/libtbb.12.dylib", f"{sdk_libs}")
    elif platform_name == "android":
        onetbb_android_root = "thirdparty/onetbb-android"
        shutil.copy(f"{env['gdextension_lib_dir']}/{env['gdextension_lib']}.so", f"{sdk_libs}")
        shutil.copy(f"{env['ext_bootstrap_lib_dir']}/{env['ext_bootstrap_lib']}.a", f"{sdk_libs}")
        shutil.copy(f"{open_usd_path}/lib/libusd_ms.so", f"{sdk_libs}")
        # oneTBB was built separately for Android
        _tbb_so = os.path.join(onetbb_android_root, "lib", "libtbb.so")
        if os.path.exists(_tbb_so):
            shutil.copy(_tbb_so, f"{sdk_libs}")
    else:
        # Linux
        shutil.copy(f"{env['gdextension_lib_dir']}/{env['gdextension_lib']}.so", f"{sdk_libs}")
        shutil.copy(f"{env['ext_bootstrap_lib_dir']}/{env['ext_bootstrap_lib']}.a",f"{sdk_libs}")
        shutil.copy(f"{open_usd_path}/lib/libusd_ms.so", f"{sdk_libs}")
        shutil.copy(f"{open_usd_path}/lib/libtbb.12.so", f"{sdk_libs}")

    # Copy the header files into the SDK folder that will be required to compile the plugin
    shutil.copytree(f"{open_usd_path}/include/pxr", f"{sdk_includes}/pxr", dirs_exist_ok=True)
    # oneTBB headers: on Android they live in a separate install prefix
    if is_android:
        onetbb_inc = "thirdparty/onetbb-android/include"
        shutil.copytree(f"{onetbb_inc}/tbb", f"{sdk_includes}/tbb", dirs_exist_ok=True)
        shutil.copytree(f"{onetbb_inc}/oneapi", f"{sdk_includes}/oneapi", dirs_exist_ok=True)
    else:
        shutil.copytree(f"{open_usd_path}/include/tbb", f"{sdk_includes}/tbb", dirs_exist_ok=True)
        shutil.copytree(f"{open_usd_path}/include/oneapi", f"{sdk_includes}/oneapi", dirs_exist_ok=True)

    shutil.copytree(f"./shared/include/idtxflow", f"{sdk_includes}/idtxflow", dirs_exist_ok=True)
    shutil.copytree(f"./shared/include/idtxflow_godot", f"{sdk_includes}/idtxflow_godot", dirs_exist_ok=True)
    
    os.makedirs(f"{sdk_includes}/idtxflow_ext", exist_ok=True)
    shutil.copy(
        "./shared/include/idtxflow_ext/ExtensionBootstrap.h",
        f"{sdk_includes}/idtxflow_ext/ExtensionBootstrap.h",
    )

    # Touch the stamp file to record completion
    pathlib.Path(str(target[0])).touch()