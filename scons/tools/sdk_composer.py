"""SCons tool: sdk_composer (refactored)

Composes the IDTXFlow Godot SDK by copying the built libraries and
required headers into one SDK output tree.

Reads everything from ``env["profile"]`` (a ``BuildProfile``).
Registers the SCons env method ``ComposeIdtxFlowGodotSDKV2``.
"""
import os
import pathlib
import shutil


def generate(env):
    env.AddMethod(_compose, "ComposeIdtxFlowGodotSDKV2")


def exists(env):
    return True


def _compose(env):
    """Register a build-phase action that depends on the previously built libs."""
    paths = env["profile"].paths
    stamp_file = f"{paths.sdk_root}/.sdk_stamp"

    stamp = env.Command(
        target=stamp_file,
        source=[],
        action=_do_compose,
    )

    if "gdextension_library_node" in env:
        env.Depends(stamp, env["gdextension_library_node"])
    if "ext_bootstrap_library_node" in env:
        env.Depends(stamp, env["ext_bootstrap_library_node"])

    env.AlwaysBuild(stamp)
    env.Default(stamp)
    return stamp


def _do_compose(target, source, env):
    print("Composing IDTXFlow SDK Artifacts")

    profile  = env["profile"]
    platform = profile.platform
    paths    = profile.paths

    os.makedirs(paths.sdk_libs, exist_ok=True)
    os.makedirs(paths.sdk_includes, exist_ok=True)

    gdext_lib   = env["gdextension_lib"]
    boot_lib    = env["ext_bootstrap_lib"]
    gdext_dir   = env["gdextension_lib_dir"]
    boot_dir    = env["ext_bootstrap_lib_dir"]
    openusd_dir = paths.openusd_install

    # ---- Copy libs ---------------------------------------------------------
    if platform.is_windows:
        shutil.copy(f"{gdext_dir}/{gdext_lib}.lib",  paths.sdk_libs)
        shutil.copy(f"{boot_dir}/{boot_lib}.lib",    paths.sdk_libs)
        shutil.copy(f"{openusd_dir}/lib/usd_ms.lib", paths.sdk_libs)
        shutil.copy(f"{openusd_dir}/lib/tbb12.lib",  paths.sdk_libs)
    elif platform.is_macos:
        shutil.copy(f"{gdext_dir}/{gdext_lib}.dylib",     paths.sdk_libs)
        shutil.copy(f"{boot_dir}/{boot_lib}.a",           paths.sdk_libs)
        shutil.copy(f"{openusd_dir}/lib/libusd_ms.dylib", paths.sdk_libs)
        shutil.copy(f"{openusd_dir}/lib/libtbb.12.dylib", paths.sdk_libs)
    elif platform.is_android:
        shutil.copy(f"{gdext_dir}/{gdext_lib}.so",     paths.sdk_libs)
        shutil.copy(f"{boot_dir}/{boot_lib}.a",        paths.sdk_libs)
        shutil.copy(f"{openusd_dir}/lib/libusd_ms.so", paths.sdk_libs)
        tbb_so = os.path.join(paths.onetbb_android, "lib", "libtbb.so")
        if os.path.exists(tbb_so):
            shutil.copy(tbb_so, paths.sdk_libs)
    else:
        # Linux
        shutil.copy(f"{gdext_dir}/{gdext_lib}.so",     paths.sdk_libs)
        shutil.copy(f"{boot_dir}/{boot_lib}.a",        paths.sdk_libs)
        shutil.copy(f"{openusd_dir}/lib/libusd_ms.so", paths.sdk_libs)
        shutil.copy(f"{openusd_dir}/lib/libtbb.12.so", paths.sdk_libs)

    # ---- Copy headers ------------------------------------------------------
    shutil.copytree(f"{openusd_dir}/include/pxr",
                    f"{paths.sdk_includes}/pxr",
                    dirs_exist_ok=True)

    if platform.is_android:
        tbb_inc = f"{paths.onetbb_android}/include"
        shutil.copytree(f"{tbb_inc}/tbb",    f"{paths.sdk_includes}/tbb",    dirs_exist_ok=True)
        shutil.copytree(f"{tbb_inc}/oneapi", f"{paths.sdk_includes}/oneapi", dirs_exist_ok=True)
    else:
        shutil.copytree(f"{openusd_dir}/include/tbb",
                        f"{paths.sdk_includes}/tbb",
                        dirs_exist_ok=True)
        shutil.copytree(f"{openusd_dir}/include/oneapi",
                        f"{paths.sdk_includes}/oneapi",
                        dirs_exist_ok=True)

    shutil.copytree(f"{paths.shared_include}/idtxflow",
                    f"{paths.sdk_includes}/idtxflow",
                    dirs_exist_ok=True)
    shutil.copytree(f"{paths.shared_include}/idtxflow_godot",
                    f"{paths.sdk_includes}/idtxflow_godot",
                    dirs_exist_ok=True)

    os.makedirs(f"{paths.sdk_includes}/idtxflow_ext", exist_ok=True)
    shutil.copy(
        f"{paths.shared_include}/idtxflow_ext/ExtensionBootstrap.h",
        f"{paths.sdk_includes}/idtxflow_ext/ExtensionBootstrap.h",
    )

    pathlib.Path(str(target[0])).touch()