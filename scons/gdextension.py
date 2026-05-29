"""
SCons tool: gdextension
Builds the GDExtension library for IDTXFlow and installs it into the addon directory for use by the Godot editor.

This step requires the OpenUSD SDK, MDL SDK, and IXWebSocket library to be built. This can be done with the
respective SCons tools for those dependencies.

Usage in SConstruct:
    env.BuildGdExtension()
"""
import configparser
import datetime
import glob
import os
import platform
import re
import shutil
import stat

def generate(env):
    env.AddMethod(_build_extension, 'BuildGdExtension')

def exists(env):
    return True

def find_absl_libs(lib_dir, extension):
    print("search abls libs")
    libs = []
    for path in glob.glob(os.path.join(lib_dir, f"*absl_*.{extension}")):
        libname = os.path.basename(path)
        if libname.endswith(f".{extension}"):
            if libname.startswith("lib"):
                lib = libname[3:-2]  # strip "lib" prefix and ".a" suffix
            else:
                lib = libname
            libs.append(lib)
    return libs

def _build_extension(env):
    print("Building Godot Extension...")

    # Get OpenUSD version from environment
    openusd_version = env.get('openusd_version', '')
    is_android = env.get('is_android', False)
    is_wasm = env.get('is_wasm', False)
    wasm_target = env.get('wasm_target', None)
    
    godot_cpp_path = "thirdparty/godot-cpp"
    mdl_sdk_path = "./thirdparty/mdl_sdk"
    ixws_path = "thirdparty/ixwebsocket"
    shared_include_path = "./shared/include"
    usd_extension_path = "usd"

    # OpenUSD install path differs for Android/WebAssembly
    if is_android:
        usd_root = f"thirdparty/openusd-{openusd_version}-android"
    elif is_wasm:
        usd_root = f"thirdparty/openusd-{openusd_version}-{wasm_target}"
    else:
        usd_root = f"thirdparty/openusd-{openusd_version}"

    platform_name = env["platform_name"]
    build_target = env["target"]
    build_arch = env["arch"]
    godotcpp_platform_name = "web" if is_wasm else platform_name

    ixws_build_dir = f"{ixws_path}/build_{platform_name}_{build_target}"
    if is_wasm:
        ixws_build_dir += "_threads"
    
    extension_env = env.Clone()

    # oneTBB install path for Android (built separately from OpenUSD)
    onetbb_android_root = "thirdparty/onetbb-android"

    # Include paths
    include_paths = [
        "source",
        "source/include",
        f"{usd_root}/include",
        f"{godot_cpp_path}/gdextension",
        f"{godot_cpp_path}/include",
        f"{godot_cpp_path}/gen/include",
        f"{shared_include_path}",
        f"{ixws_path}",
        f"{usd_extension_path}/include",
    ]
    # MDL SDK headers only on desktop platforms
    if not is_android and not is_wasm:
        include_paths.append(f"{mdl_sdk_path}/include")
    # Android: oneTBB headers are in a separate install prefix
    if is_android:
        include_paths.append(f"{onetbb_android_root}/include")
    extension_env.Append(CPPPATH=include_paths)
        
    # OpenSSL library/include paths (platform-specific)
    # prefer system/Homebrew OpenSSL, fall back to vcpkg install
    if platform_name == "windows":
        # vcpkg-installed OpenSSL (always used on Windows)
        vcpkg_triplet = "x64-windows-static"
        vcpkg_installed = os.path.join("thirdparty", "vcpkg", "installed", vcpkg_triplet)
        extension_env.Append(LIBPATH=[os.path.join(vcpkg_installed, "lib")])
        extension_env.Append(CPPPATH=[os.path.join(vcpkg_installed, "include")])
    elif platform_name == "macos":
        # Try Homebrew OpenSSL first
        _ssl_found = False
        homebrew_openssl_candidates = [
            "/opt/homebrew/opt/openssl",
            "/usr/local/opt/openssl",
            "/opt/homebrew/opt/openssl@3",
            "/usr/local/opt/openssl@3",
        ]
        for candidate in homebrew_openssl_candidates:
            if os.path.isdir(candidate):
                extension_env.Append(LIBPATH=[os.path.join(candidate, "lib")])
                extension_env.Append(CPPPATH=[os.path.join(candidate, "include")])
                _ssl_found = True
                break
        if not _ssl_found:
            # Fall back to vcpkg-installed OpenSSL
            _machine = platform.machine().lower()
            _triplet = "arm64-osx" if _machine in ("arm64", "aarch64") else "x64-osx"
            _vcpkg_installed = os.path.join("thirdparty", "vcpkg", "installed", _triplet)
            if os.path.isdir(_vcpkg_installed):
                extension_env.Append(LIBPATH=[os.path.join(_vcpkg_installed, "lib")])
                extension_env.Append(CPPPATH=[os.path.join(_vcpkg_installed, "include")])
    elif platform_name == "linux":
        # System OpenSSL dev headers present? If not, use vcpkg
        if not os.path.isfile("/usr/include/openssl/ssl.h") and not os.path.isfile("/usr/local/include/openssl/ssl.h"):
            _machine = platform.machine().lower()
            _triplet = "arm64-linux" if _machine in ("arm64", "aarch64") else "x64-linux"
            _vcpkg_installed = os.path.join("thirdparty", "vcpkg", "installed", _triplet)
            if os.path.isdir(_vcpkg_installed):
                extension_env.Append(LIBPATH=[os.path.join(_vcpkg_installed, "lib")])
                extension_env.Append(CPPPATH=[os.path.join(_vcpkg_installed, "include")])
        
    # Library paths
    lib_paths = [
        f"{usd_root}/lib",
        f"{godot_cpp_path}/bin",
        f"{ixws_build_dir}/Release" if platform_name == "windows" else f"{ixws_build_dir}",
        f"{usd_extension_path}/libs/{platform_name}",        
    ]

    # MDL SDK lib path only on desktop platforms
    if not is_android and not is_wasm:
        lib_paths.append(f"{mdl_sdk_path}/lib")
    # Android: oneTBB libs are in a separate install prefix
    if is_android:
        lib_paths.append(f"{onetbb_android_root}/lib")
    extension_env.Append(LIBPATH=lib_paths)

    
    # Base libraries
    if is_android:
        libs = [
            "usd_ms", "tbb",
            "libidtx_usd",
            f"libgodot-cpp.{godotcpp_platform_name}.{build_target}.{build_arch}",
            "ixwebsocket",
        ]
    elif is_wasm:
        libs = [
            "usd_m", "tbb",
            "libidtx_usd",
            "ixwebsocket",
        ]
    else:
        libs = [
            "usd_ms", "tbb12" if platform_name == "windows" else "tbb.12",
            "libidtx_usd",
            f"libgodot-cpp.{godotcpp_platform_name}.{build_target}.{build_arch}",
            "ixwebsocket",
        ]

    # OpenSSL static libs (desktop platforms only — TLS disabled on Android/Wasm)
    if not is_android and not is_wasm:
        if platform_name == "windows":
            # vcpkg static OpenSSL lib names on Windows
            libs.extend(["libssl", "libcrypto"])
        else:
            # Linux/macOS: standard OpenSSL lib names
            libs.extend(["ssl", "crypto"])

    # generic build flags
    if is_android:
        extension_env.Append(CXXFLAGS=['-fexceptions', '-frtti', '-std=c++20'])
        extension_env.Append(CCFLAGS=["-O3" if build_target == "template_release" else "-g"])
    elif platform.system() == "Windows" and (env["CXX"] == "cl" or env["CC"] == "cl"):
        extension_env.Append(CXXFLAGS=['/EHsc', '/GR', '/FS', '/arch:AVX2', '/std:c++20'])        
    else:
        extension_env.Append(CXXFLAGS=['-fexceptions', '-frtti', '-g', '-std=c++20'])
        extension_env.Append(CCFLAGS=["-O3" if build_target == "template_release" else "-g"])

    extension_env.Append(CPPDEFINES=["IDTXFLOW_ENABLED", "IDTXFLOW_GODOT_EXPORTS", "THREADS_ENABLED", "GDEXTENSION"])

    # Platform-specific configuration
    if platform_name == "android":
        extension_env.Append(LIBS=libs + ["log", "android", "dl", "m"])
        extension_env.Append(CCFLAGS=["-fPIC", "-frtti"])
        extension_env.Append(LINKFLAGS=["-Wl,-z,relro", "-Wl,-z,now"])

    elif platform_name == "wasm32" or platform_name == "wasm64":
        # WebAssembly: no OpenSSL/MDL, keep link surface minimal.
        extension_env.Append(LIBS=libs)
        # Use JS-based exception model (no -fwasm-exceptions): Godot's web template does not
        # provide __cpp_exception as a WebAssembly.Tag, so native wasm EH cannot be used.
        # invoke_* stubs (generated by -fexceptions for virtual/function-pointer calls) are
        # provided by source/wasm_shims/invoke_shims.cpp, compiled with -fno-exceptions to
        # prevent recursive invoke_* wrapping. See the "Source files" section below.
        # __hash_memory (std::__2::__hash_memory) and other libc++ internals not exported
        # by Godot are provided by source/wasm_shims/libcxx_hash_shim.cpp, which is compiled
        # with the correct PIC/atomics flags as part of the extension source.
        extension_env.Append(CCFLAGS=["-frtti", "-pthread"])
        extension_env.Append(LINKFLAGS=["-pthread", "-sALLOW_MEMORY_GROWTH=1"])
        if platform_name == "wasm64":
            extension_env.Append(CCFLAGS=["-sWASM64=1"])
            extension_env.Append(LINKFLAGS=["-sWASM64=1"])

    elif platform_name == "linux":
        extension_env.Append(LIBS=libs + ["dl", "pthread", "m"])
        extension_env.Append(CCFLAGS=["-fPIC", "-g", "-frtti"])
        extension_env.Append(LINKFLAGS=["-Wl,-rpath,$ORIGIN"])

    elif platform_name == "windows":
        # ws2_32, crypt32, user32 are required by IXWebSocket + OpenSSL on Windows
        extension_env.Append(LIBS=libs + ["advapi32", "shell32", "ole32", "ws2_32", "crypt32", "user32"])
        extension_env.Append(CPPDEFINES=["NOMINMAX", "WIN32_LEAN_AND_MEAN", "_ITERATOR_DEBUG_LEVEL=0"])
        if build_target in ["editor", "template_debug"]:
            # DEBUG
            extension_env.Append(CCFLAGS=[
                "/Zi",        # debug symbols
                "/Od",        # no optimization
                "/EHsc",
                "/MT"
            ])
            extension_env.Append(LINKFLAGS=[
                "/DEBUG"      # generate PDB (REQUIRED)
            ])
        else:
            # RELEASE
            extension_env.Append(CCFLAGS=[
                "/O2",
                "/EHsc",
                "/MT"
            ])
    elif platform_name == "macos":
        extension_env.Append(LIBS=libs)
        extension_env.Append(CCFLAGS=["-fPIC", "-g", "-Og", "-O0", "-frtti"])
        extension_env.Append(LINKFLAGS=["-framework", "CoreFoundation"])
        extension_env.Append(LINKFLAGS=["-install_name", "@rpath/libidtxflow.dylib", "-Wl,-rpath,@loader_path"])
        extension_env.Append(LINKFLAGS=["-g"])        

    # Source files
    sources = list(set(extension_env.Glob("source/*.cpp") + extension_env.Glob("source/**/*.cpp")))
    # filter the source files in the gen subfolder
    exclude_dir = os.path.normpath("source/gen")
    try:
        sources = [s for s in sources if not os.path.commonpath([s.get_dir().get_path(), exclude_dir]) == exclude_dir]
    except ValueError:
        # Handle case where paths are on different drives - just exclude by simple path check
        sources = [s for s in sources if exclude_dir not in s.get_dir().get_path()]

    # For wasm builds: invoke_shims.cpp must be compiled with -fno-exceptions to avoid
    # infinite recursion. When compiled with -fexceptions, function pointer calls inside
    # invoke_* would themselves generate invoke_* wrapper calls -> stack overflow.
    # Since Godot uses -fno-exceptions (__cxa_throw -> abort()), the try/catch in the
    # standard invoke_* implementation is never needed; direct call_indirect is sufficient.
    if is_wasm:
        invoke_shims_src = "source/wasm_shims/invoke_shims.cpp"
        if os.path.exists(invoke_shims_src):
            # Remove invoke_shims.cpp from the regular (fexceptions) sources list
            invoke_shims_norm = os.path.normpath(invoke_shims_src)
            sources = [s for s in sources
                       if os.path.normpath(str(s).replace('\\', '/')) != invoke_shims_norm
                       and os.path.normpath(s.get_path()) != invoke_shims_norm]
            # Build invoke_shims.cpp with -fno-exceptions (direct call_indirect, no JS try/catch)
            invoke_shims_env = extension_env.Clone()
            cxxflags_no_exc = [f for f in list(invoke_shims_env['CXXFLAGS']) if f != '-fexceptions']
            cxxflags_no_exc.append('-fno-exceptions')
            invoke_shims_env.Replace(CXXFLAGS=cxxflags_no_exc)
            sources.append(invoke_shims_env.SharedObject(invoke_shims_src))
    
    if build_target in ["editor", "template_debug"]:
        print("Generating doc data..")
        try:
            doc_data = extension_env.GodotCPPDocData("source/gen/doc_data.gen.cpp", source=extension_env.Glob("doc_classes/*.xml"))
            sources.append(doc_data)
        except AttributeError as e:
            print(f"Not including class reference as we're targeting a pre-4.3 baseline. Error: {e}")


    # Output library name
    library_name = f"libidtxflow.{godotcpp_platform_name}.{build_target}.{build_arch}"
    library_extension = "dll" if platform_name == "windows" else ("dylib" if platform_name == "macos" else ("wasm" if is_wasm else "so"))
    
    # Set build directory
    build_dir = f"build/IDTXFlow/bin/{platform_name}"
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)

    # Build the library
    library = extension_env.SharedLibrary(f"{build_dir}/{library_name}.{library_extension}", sources)

    # Determine PDB path
    pdb_file = None
    if platform_name == "windows" and build_target in ["editor", "template_debug"]:
        dll_path = library[0].abspath
        pdb_file = os.path.splitext(dll_path)[0] + ".pdb"

    # Add install target
    install_dir = f"addon/IDTXFlow/bin/{platform_name}"
    install_targets = list(library)
    if pdb_file and os.path.exists(pdb_file):
        install_targets.append(extension_env.File(pdb_file))

    install_ext = extension_env.Install(install_dir, install_targets)
    install_libs = extension_env.Install(install_dir, _get_libs_to_install(platform_name, openusd_version, is_android, is_wasm, wasm_target))
    extension_env.AddPostAction(library, _copy_usd_plugins)
    extension_env.AddPostAction(library, _copy_third_party_licenses)

    extension_env.Default(library, install_ext + install_libs)

    # Store the library name and node in the environment so idtxflow_sdk.py can reference it
    env['gdextension_lib'] = library_name
    env['gdextension_lib_dir'] = os.path.abspath(build_dir)
    env['gdextension_library_node'] = library


def _get_libs_to_install(platform_name, openusd_version="", is_android=False, is_wasm=False, wasm_target=None):
    print("Getting libs to install...")
    mdl_sdk_root = f"./thirdparty/mdl_sdk"

    if is_android:
        usd_root = f"./thirdparty/openusd-{openusd_version}-android"
        onetbb_root = "./thirdparty/onetbb-android"
        # Android: USD libs + oneTBB (built separately), no MDL SDK
        libs_to_install = [
            f"./usd/build/{platform_name}/libidtx_usd.so",
            f"{usd_root}/lib/libusd_ms.so",
            f"{onetbb_root}/lib/libtbb.so",
        ]
    elif is_wasm:
        usd_root = f"./thirdparty/openusd-{openusd_version}-{wasm_target}"
        # WebAssembly: package static dependency archives (no MDL on wasm).
        libs_to_install = [
            f"{usd_root}/lib/libusd_m.a",
            f"{usd_root}/lib/libtbb.a",
            f"./usd/build/{platform_name}/libidtx_usd.a",
        ]
    elif platform_name == "windows":
        usd_root = f"./thirdparty/openusd-{openusd_version}"
        libs_to_install = [
            f"{usd_root}/lib/usd_ms.dll",
            f"{usd_root}/bin/tbb12.dll",
            f"{mdl_sdk_root}/bin/libmdl_core.dll",
            f"{mdl_sdk_root}/bin/libmdl_sdk.dll",
            f"{mdl_sdk_root}/bin/dds.dll",
            f"{mdl_sdk_root}/bin/nv_openimageio.dll",
            f"{mdl_sdk_root}/bin/mdl_distiller.dll",
            f"./usd/build/{platform_name}/libidtx_usd.dll",
        ]
    elif platform_name == "macos":
        usd_root = f"./thirdparty/openusd-{openusd_version}"
        libs_to_install = [
            f"{usd_root}/lib/libusd_ms.dylib",
            f"{usd_root}/lib/libtbb.12.dylib",
            f"{mdl_sdk_root}/lib/libmdl_core.so",
            f"{mdl_sdk_root}/lib/libmdl_sdk.so",
            f"{mdl_sdk_root}/lib/dds.so",
            f"{mdl_sdk_root}/lib/nv_openimageio.so",
            f"{mdl_sdk_root}/lib/mdl_distiller.so",
            f"./usd/build/{platform_name}/libidtx_usd.dylib",
        ]
    else:
        usd_root = f"./thirdparty/openusd-{openusd_version}"
        libs_to_install = [
            f"{usd_root}/lib/libusd_ms.so",
            f"{usd_root}/lib/libtbb12.so",
            f"{mdl_sdk_root}/lib/libmdl_core.so",
            f"{mdl_sdk_root}/lib/libmdl_sdk.so",
            f"{mdl_sdk_root}/lib/dds.so",
            f"{mdl_sdk_root}/lib/nv_openimageio.so",
            f"{mdl_sdk_root}/lib/mdl_distiller.so",
            f"./usd/build/{platform_name}/libidtx_usd.so",
        ]

    return libs_to_install

def _copy_usd_plugins(target, source, env):
    print("Copy USD Plugin Config..")
    is_android = env.get('is_android', False)
    is_wasm = env.get('is_wasm', False)
    wasm_target = env.get('wasm_target', None)
    openusd_version = env.get('openusd_version', '')
    platform_name = env['platform_name']

    if is_android:
        usd_root = f"./thirdparty/openusd-{openusd_version}-android"
    elif is_wasm:
        usd_root = f"./thirdparty/openusd-{openusd_version}-{wasm_target}"
    else:
        usd_root = f"./thirdparty/openusd-{openusd_version}"

    usd_lib_plugins = os.path.join(usd_root, "lib", "usd")
    usd_plugin_dir = os.path.join(usd_root, "plugin", "usd")

    if os.path.isdir(usd_lib_plugins):
        shutil.copytree(usd_lib_plugins, f"addon/IDTXFlow/bin/{platform_name}/usd", dirs_exist_ok=True)
    if os.path.isdir(usd_plugin_dir):
        shutil.copytree(usd_plugin_dir, f"addon/IDTXFlow/bin/plugin/usd", dirs_exist_ok=True)
    shutil.copytree("usd/plugin/godot", "addon/IDTXFlow/bin/plugin/usd/godot", dirs_exist_ok=True)
    shutil.copytree("usd/plugin/idtx", "addon/IDTXFlow/bin/plugin/usd/idtx", dirs_exist_ok=True)

def _copy_third_party_licenses(target, source, env):
    """Copy third-party LICENSE files to addon for distribution compliance."""
    print("Copying third-party LICENSE files...")

    license_dest_dir = "addon/IDTXFlow/LICENSES-THIRD-PARTY"
    os.makedirs(license_dest_dir, exist_ok=True)

    openusd_version = env.get('openusd_version', '')
    license_files = [
        ("thirdparty/godot-cpp/LICENSE.md", "godot-cpp-LICENSE.md"),
        ("thirdparty/ixwebsocket/LICENSE.txt", "ixwebsocket-LICENSE.txt"),
        ("thirdparty/mdl_sdk/LICENSE.md", "mdl-sdk-LICENSE.md"),
        ("thirdparty/mdl_sdk/LICENSE_THIRDPARTY.md", "mdl-sdk-LICENSE_THIRDPARTY.md"),
        (f"thirdparty/openusd-{openusd_version}-src/LICENSE.txt", "openusd-LICENSE.txt"),
        (f"thirdparty/openusd-{openusd_version}-src/NOTICE.txt", "openusd-NOTICE.txt"),
    ]

    missing = []
    for src, dest_name in license_files:
        if os.path.exists(src):
            dest_path = os.path.join(license_dest_dir, dest_name)
            shutil.copy2(src, dest_path)
            # copy2 preserves source permissions; some SDKs ship read-only files,
            # which would cause a Permission denied error on the next incremental build.
            os.chmod(dest_path, os.stat(dest_path).st_mode | stat.S_IRUSR | stat.S_IWUSR)
            print(f"  Copied: {src} -> {dest_path}")
        else:
            missing.append(src)

    if os.path.exists("THIRDPARTY.txt"):
        cfg = configparser.ConfigParser()
        cfg.read("addon/IDTXFlow/plugin.cfg")
        version = cfg.get("plugin", "version", fallback="unknown").strip('"').strip("'")
        if not re.fullmatch(r"\d+\.\d+\.\d+(?:-[\w.]+)?(?:\+[\w.]+)?", version):
            print(f"ERROR: Version '{version}' in plugin.cfg does not follow semver (MAJOR.MINOR.PATCH).")
            return 1
        today = datetime.date.today()
        date_str = f"{today.strftime('%B')} {today.day}, {today.year}"
        with open("THIRDPARTY.txt", "r") as f:
            lines = f.read().splitlines(keepends=True)
        lines[0] = f"IDTX Flow - Version {version} - {date_str}\n"
        with open("addon/IDTXFlow/THIRDPARTY.txt", "w") as f:
            f.writelines(lines)
        print(f"  Stamped THIRDPARTY.txt with version {version} and date {date_str}")

    if missing:
        print("ERROR: The following LICENSE files are missing and must be present for distribution compliance:")
        for f in missing:
            print(f"  {f}")
        return 1

