"""SCons tool: gdextension (refactored)

Builds the main IDTXFlow GDExtension shared library, installs it into the
addon directory, and copies USD plugin metadata + third-party LICENSE
files into the addon for distribution.

Reads everything from ``env["profile"]`` (a ``BuildProfile``).
Registers the SCons env method ``BuildGdExtensionV2``.
"""
import configparser
import datetime
import glob
import os
import re
import shutil
import stat

from scons.core.apply import apply_profile_section
from scons.core import openssl_locator


def generate(env):
    env.AddMethod(_build_extension, "BuildGdExtensionV2")


def exists(env):
    return True


# ---------------------------------------------------------------------------
# Main build entry point
# ---------------------------------------------------------------------------

def _build_extension(env):
    print("Building Godot Extension...")

    profile  = env["profile"]
    platform = profile.platform
    paths    = profile.paths

    extension_env = env.Clone()

    # Protobuf: runtime include dir + the generated idtxcore.* sources directory.
    # BuildProtobuf() publishes the env keys; GenerateProtoSources() writes the
    # *.pb.{cc,h} into proto_gen_dir.
    protobuf_include_dir = env.get("protobuf_include_dir", "")
    protobuf_lib_dir     = env.get("protobuf_lib_dir", "")
    protobuf_lib_name    = env.get("protobuf_lib_name", "")
    proto_gen_dir = os.path.join("shared", "src", "idtxflow", "net", "proto_gen")

    # ---- Include paths ------------------------------------------------------
    include_paths = [
        "source",
        "source/include",
        f"{paths.openusd_install}/include",
        f"{paths.godot_cpp}/gdextension",
        f"{paths.godot_cpp}/include",
        f"{paths.godot_cpp}/gen/include",
        paths.shared_include,
        paths.ixwebsocket,
        paths.usd_ext_include,
        proto_gen_dir,  # generated *.pb.h live alongside the *.pb.cc
    ]
    if protobuf_include_dir:
        include_paths.append(protobuf_include_dir)
    if not platform.is_android:
        include_paths.append(f"{paths.mdl_sdk}/include")
    if platform.is_android:
        include_paths.append(f"{paths.onetbb_android}/include")
        include_paths.append(f"{paths.mbedtls_install}/include")
    extension_env.Append(CPPPATH=include_paths)

    # ---- OpenSSL include/lib (lazy probe, only when actually needed) -------
    openssl = None
    if not platform.is_android:
        openssl = openssl_locator.find_openssl(platform)
        if openssl:
            extension_env.Append(LIBPATH=[openssl.lib_dir])
            extension_env.Append(CPPPATH=[openssl.include_dir])

    # ---- Library paths ------------------------------------------------------
    ixws_release_dir = (f"{paths.ixwebsocket_build}/Release"
                        if platform.is_windows else paths.ixwebsocket_build)
    lib_paths = [
        f"{paths.openusd_install}/lib",
        f"{paths.godot_cpp}/bin",
        ixws_release_dir,
        # idtx_usd import lib is installed here by usd_extension.py. NOTE: do not
        # use paths.usd_ext_libs here — that dir can hold a stale lib built against
        # a different OPENUSD_VERSION, producing pxrInternal_v0_* namespace
        # mismatches at link time.
        f"{paths.shared_libs}/{platform.name}",
    ]
    if protobuf_lib_dir:
        lib_paths.append(protobuf_lib_dir)
    if not platform.is_android:
        lib_paths.append(f"{paths.mdl_sdk}/lib")
    if platform.is_android:
        lib_paths.append(f"{paths.onetbb_android}/lib")
        lib_paths.append(f"{paths.mbedtls_install}/lib")
    extension_env.Append(LIBPATH=lib_paths)

    # ---- Libraries ----------------------------------------------------------
    libs = [
        "usd_ms",
        profile.tbb_link_name,
        f"{profile.platform.shared_lib_prefix}idtx_usd",
        f"libgodot-cpp.{platform.name}.{platform.target}.{platform.arch}",
        "ixwebsocket",
    ]
    if not platform.is_android:
        libs.extend(openssl_locator.link_libnames(platform))
    else:
        # IXWebSocket's MbedTLS backend. Order matters for static linking:
        # ssl -> x509 -> crypto, and all after "ixwebsocket" (which references them).
        libs.extend(["mbedtls", "mbedx509", "mbedcrypto"])

    # protobuf runtime (static) for the idtxcore.* generated messages
    if protobuf_lib_name:
        libs.append(protobuf_lib_name)

    # ---- Apply pre-computed profile sections (no decisions here) -----------
    apply_profile_section(extension_env, profile.cxx_flags)
    apply_profile_section(extension_env, profile.shared_lib_flags)
    apply_profile_section(extension_env, profile.msvc_extras)
    apply_profile_section(extension_env, profile.gdextension_extras)

    extension_env.Append(CPPDEFINES=[
        "IDTXFLOW_ENABLED",
        "IDTXFLOW_GODOT_EXPORTS",
        "THREADS_ENABLED",
        "GDEXTENSION",
    ])

    library_basename = f"idtxflow.{platform.name}.{platform.target}.{platform.arch}"
    library_full     = profile.shared_lib_filename(library_basename)

    if platform.is_macos:
        # macOS install_name needs the per-library filename
        extension_env.Append(LINKFLAGS=["-install_name", f"@rpath/{library_full}"])

    extension_env.Append(LIBS=libs)

    # ---- Sources ------------------------------------------------------------
    sources = list(set(
        extension_env.Glob("source/*.cpp") + extension_env.Glob("source/**/*.cpp")
    ))
    exclude_dir = os.path.normpath("source/gen")
    try:
        sources = [s for s in sources
                   if os.path.commonpath([s.get_dir().get_path(), exclude_dir]) != exclude_dir]
    except ValueError:
        sources = [s for s in sources if exclude_dir not in s.get_dir().get_path()]

    # Engine-agnostic sources under shared/src are not covered by the source/**
    # glob above, so add them explicitly. (The refactored tool globs only
    # source/**; these shared/ trees must be listed here or their symbols —
    # CollabEngine, StagePersistence, the idtxcore.* wire messages — go
    # unresolved at link time.)

    # Generated protobuf sources (idtxcore.* wire messages).
    sources += extension_env.Glob(os.path.join(proto_gen_dir, "*.pb.cc"))

    # Collaboration net stack (net/, wire/, adapters/**) and stage operations.
    # SCons Glob does not recurse through '**', so use Python's recursive glob.
    # The *.pb.cc above match only *.cpp here (so they are not double-added) and
    # the set()-dedup on `sources` guards any overlap.
    for shared_glob in (
        os.path.join("shared", "src", "idtxflow", "net", "**", "*.cpp"),
        os.path.join("shared", "src", "idtxflow", "stage_ops", "**", "*.cpp"),
    ):
        sources += [extension_env.File(p) for p in glob.glob(shared_glob, recursive=True)]

    if platform.target in ("editor", "template_debug"):
        print("Generating doc data..")
        try:
            doc_data = extension_env.GodotCPPDocData(
                "source/gen/doc_data.gen.cpp",
                source=extension_env.Glob("doc_classes/*.xml"),
            )
            sources.append(doc_data)
        except AttributeError as e:
            print(f"Not including class reference (pre-4.3 baseline). Error: {e}")

    # ---- Build the shared library ------------------------------------------
    build_dir = paths.gdext_build
    os.makedirs(build_dir, exist_ok=True)

    library = extension_env.SharedLibrary(f"{build_dir}/{library_full}", sources)

    pdb_file = None
    if platform.is_windows and platform.target in ("editor", "template_debug"):
        dll_path = library[0].abspath
        pdb_file = os.path.splitext(dll_path)[0] + ".pdb"

    # ---- Install ------------------------------------------------------------
    install_targets = list(library)
    if pdb_file and os.path.exists(pdb_file):
        install_targets.append(extension_env.File(pdb_file))

    install_ext  = extension_env.Install(paths.addon_install, install_targets)
    install_libs = extension_env.Install(paths.addon_install,
                                         _get_libs_to_install(platform, paths))
    extension_env.AddPostAction(library, _copy_usd_plugins)
    extension_env.AddPostAction(library, _copy_third_party_licenses)

    extension_env.Default(library, install_ext + install_libs)

    # Stash for the SDK composer
    env["gdextension_lib"]           = library_basename
    env["gdextension_lib_filename"]  = library_full
    env["gdextension_lib_dir"]       = os.path.abspath(build_dir)
    env["gdextension_library_node"]  = library


# ---------------------------------------------------------------------------
# Post-actions and install helpers
# ---------------------------------------------------------------------------

def _get_libs_to_install(platform, paths):
    """Return the list of runtime libs that must be copied into the addon dir."""
    if platform.is_android:
        return [
            f"{paths.openusd_install}/lib/libusd_ms.so",
            f"{paths.onetbb_android}/lib/libtbb.so",
            f"{paths.usd_ext_build}/{platform.shared_lib_prefix}idtx_usd.so",
        ]
    if platform.is_windows:
        return [
            f"{paths.openusd_install}/lib/usd_ms.dll",
            f"{paths.openusd_install}/bin/tbb12.dll",
            f"{paths.mdl_sdk}/bin/libmdl_core.dll",
            f"{paths.mdl_sdk}/bin/libmdl_sdk.dll",
            f"{paths.mdl_sdk}/bin/dds.dll",
            f"{paths.mdl_sdk}/bin/nv_openimageio.dll",
            f"{paths.mdl_sdk}/bin/mdl_distiller.dll",
            f"{paths.usd_ext_build}/{platform.shared_lib_prefix}idtx_usd.dll",
        ]
    if platform.is_macos:
        return [
            f"{paths.openusd_install}/lib/libusd_ms.dylib",
            f"{paths.openusd_install}/lib/libtbb.12.dylib",
            f"{paths.mdl_sdk}/lib/libmdl_core.so",
            f"{paths.mdl_sdk}/lib/libmdl_sdk.so",
            f"{paths.mdl_sdk}/lib/dds.so",
            f"{paths.mdl_sdk}/lib/nv_openimageio.so",
            f"{paths.mdl_sdk}/lib/mdl_distiller.so",
            f"{paths.usd_ext_build}/{platform.shared_lib_prefix}idtx_usd.dylib",
        ]
    # Linux
    return [
        f"{paths.openusd_install}/lib/libusd_ms.so",
        f"{paths.openusd_install}/lib/libtbb12.so",
        f"{paths.mdl_sdk}/lib/libmdl_core.so",
        f"{paths.mdl_sdk}/lib/libmdl_sdk.so",
        f"{paths.mdl_sdk}/lib/dds.so",
        f"{paths.mdl_sdk}/lib/nv_openimageio.so",
        f"{paths.mdl_sdk}/lib/mdl_distiller.so",
        f"{paths.usd_ext_build}/{platform.shared_lib_prefix}idtx_usd.so",
    ]


def _copy_usd_plugins(target, source, env):
    """Copy USD plugin config trees into the addon directory."""
    print("Copy USD Plugin Config..")
    paths = env["profile"].paths

    usd_lib_plugins = os.path.join(paths.openusd_install, "lib", "usd")
    usd_plugin_dir  = os.path.join(paths.openusd_install, "plugin", "usd")

    if os.path.isdir(usd_lib_plugins):
        shutil.copytree(usd_lib_plugins,
                        f"{paths.addon_install}/usd",
                        dirs_exist_ok=True)
    if os.path.isdir(usd_plugin_dir):
        shutil.copytree(usd_plugin_dir,
                        paths.addon_plugin_install,
                        dirs_exist_ok=True)

    shutil.copytree("usd/plugin/godot",
                    f"{paths.addon_plugin_install}/godot",
                    dirs_exist_ok=True)
    shutil.copytree("usd/plugin/idtx",
                    f"{paths.addon_plugin_install}/idtx",
                    dirs_exist_ok=True)


def _copy_third_party_licenses(target, source, env):
    """Copy third-party LICENSE files to the addon for distribution compliance."""
    print("Copying third-party LICENSE files...")
    paths = env["profile"].paths

    license_dest_dir = "addons/IDTXFlow/LICENSES-THIRD-PARTY"
    os.makedirs(license_dest_dir, exist_ok=True)

    license_files = [
        (f"{paths.godot_cpp}/LICENSE.md",          "godot-cpp-LICENSE.md"),
        (f"{paths.ixwebsocket}/LICENSE.txt",       "ixwebsocket-LICENSE.txt"),
        (f"{paths.mdl_sdk}/LICENSE.md",            "mdl-sdk-LICENSE.md"),
        (f"{paths.mdl_sdk}/LICENSE_THIRDPARTY.md", "mdl-sdk-LICENSE_THIRDPARTY.md"),
        (f"{paths.openusd_src}/LICENSE.txt",       "openusd-LICENSE.txt"),
        (f"{paths.openusd_src}/NOTICE.txt",        "openusd-NOTICE.txt"),
    ]

    missing = []
    for src, dest_name in license_files:
        if os.path.exists(src):
            dest_path = os.path.join(license_dest_dir, dest_name)
            shutil.copy2(src, dest_path)
            os.chmod(dest_path,
                     os.stat(dest_path).st_mode | stat.S_IRUSR | stat.S_IWUSR)
            print(f"  Copied: {src} -> {dest_path}")
        else:
            missing.append(src)

    if os.path.exists("THIRDPARTY.txt"):
        cfg = configparser.ConfigParser()
        cfg.read("addons/IDTXFlow/plugin.cfg")
        version = cfg.get("plugin", "version", fallback="unknown").strip('"').strip("'")
        if not re.fullmatch(r"\d+\.\d+\.\d+(?:-[\w.]+)?(?:\+[\w.]+)?", version):
            print(f"ERROR: Version '{version}' in plugin.cfg does not follow semver.")
            return 1
        today = datetime.date.today()
        date_str = f"{today.strftime('%B')} {today.day}, {today.year}"
        with open("THIRDPARTY.txt", "r") as f:
            lines = f.read().splitlines(keepends=True)
        lines[0] = f"IDTX Flow - Version {version} - {date_str}\n"
        with open("addons/IDTXFlow/THIRDPARTY.txt", "w") as f:
            f.writelines(lines)
        print(f"  Stamped THIRDPARTY.txt with version {version} and date {date_str}")

    if missing:
        print("ERROR: The following LICENSE files are missing:")
        for f in missing:
            print(f"  {f}")
        return 1
