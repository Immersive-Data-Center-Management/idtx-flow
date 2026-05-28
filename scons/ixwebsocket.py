"""
SCons tool: ixwebsocket
Builds the IXWebSocket library from source using CMake. This is required for HTTPClient and WebSocket support
in the IDTXFlow GDExtension.

Usage in SConstruct:
    env.BuildIXWebSocket()
"""
import os
import platform
import subprocess
import shutil
from SCons.Script import Exit

from download_utils import download_file, extract_archive

IXWEBSOCKET_VERSION = "v11.4.6"
IXWEBSOCKET_SHA256 = "c024334f8e45980836c67008979a884d6dcc5ef067dd2eb1fa7241f4c17ddc32"
BASE_URL = "https://github.com/machinezone/IXWebSocket/archive/refs/tags"


def generate(env):
    env.AddMethod(_build_ixwebsocket, 'BuildIXWebSocket')


def exists(env):
    return True


# System OpenSSL detection

def _find_system_openssl(platform_name):
    """
    Probe for an existing OpenSSL installation on macOS / Linux.

    Returns (include_dir, lib_dir) if found, or None.
    On Windows this always returns None (vcpkg is used instead).
    """
    if platform_name == "windows":
        return None

    if platform_name == "macos":
        # Homebrew locations (Apple Silicon first, then Intel, then @3 variants)
        candidates = [
            "/opt/homebrew/opt/openssl",
            "/usr/local/opt/openssl",
            "/opt/homebrew/opt/openssl@3",
            "/usr/local/opt/openssl@3",
        ]
        for prefix in candidates:
            header = os.path.join(prefix, "include", "openssl", "ssl.h")
            lib_dir = os.path.join(prefix, "lib")
            if os.path.isfile(header) and os.path.isdir(lib_dir):
                return (os.path.join(prefix, "include"), lib_dir)

        # Fall back to pkg-config
        return _probe_openssl_pkg_config()

    # Linux
    # Standard distro paths
    linux_candidates = [
        ("/usr/include", "/usr/lib/x86_64-linux-gnu"),
        ("/usr/include", "/usr/lib64"),
        ("/usr/include", "/usr/lib"),
        ("/usr/local/include", "/usr/local/lib"),
    ]
    for inc, lib in linux_candidates:
        header = os.path.join(inc, "openssl", "ssl.h")
        if os.path.isfile(header) and os.path.isdir(lib):
            return (inc, lib)

    # Fall back to pkg-config
    return _probe_openssl_pkg_config()


def _probe_openssl_pkg_config():
    """Use pkg-config to locate OpenSSL. Returns (include_dir, lib_dir) or None."""
    try:
        inc = subprocess.check_output(
            ["pkg-config", "--variable=includedir", "openssl"],
            stderr=subprocess.DEVNULL, text=True
        ).strip()
        lib = subprocess.check_output(
            ["pkg-config", "--variable=libdir", "openssl"],
            stderr=subprocess.DEVNULL, text=True
        ).strip()
        if inc and lib and os.path.isfile(os.path.join(inc, "openssl", "ssl.h")):
            return (inc, lib)
    except (subprocess.CalledProcessError, FileNotFoundError):
        pass
    return None


def _find_emsdk_ninja(emsdk_root):
    """Locate Ninja inside EMSDK install on Windows/macOS/Linux."""
    exe_name = "ninja.exe" if platform.system() == "Windows" else "ninja"

    # Common EMSDK layouts
    candidates = [
        os.path.join(emsdk_root, "upstream", "emscripten", exe_name),
        os.path.join(emsdk_root, exe_name),
    ]

    ninja_root = os.path.join(emsdk_root, "ninja")
    if os.path.isdir(ninja_root):
        for entry in sorted(os.listdir(ninja_root), reverse=True):
            candidates.append(os.path.join(ninja_root, entry, exe_name))

    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate

    # Fall back to PATH
    ninja_in_path = shutil.which(exe_name)
    if ninja_in_path:
        return ninja_in_path

    # Fall back to vcpkg's downloaded ninja (commonly present in this repo)
    vcpkg_ninja_root = os.path.abspath(os.path.join("thirdparty", "vcpkg", "downloads", "tools"))
    if os.path.isdir(vcpkg_ninja_root):
        for entry in sorted(os.listdir(vcpkg_ninja_root), reverse=True):
            candidate = os.path.join(vcpkg_ninja_root, entry, exe_name)
            if os.path.isfile(candidate):
                return candidate

    return None


# vcpkg helpers (Windows always, Linux/macOS as fallback)

def _ensure_vcpkg():
    """Clone and bootstrap vcpkg into thirdparty/vcpkg if not already present."""
    vcpkg_root = os.path.abspath("thirdparty/vcpkg")
    if os.path.exists(os.path.join(vcpkg_root, "vcpkg.exe")) or os.path.exists(os.path.join(vcpkg_root, "vcpkg")):
        return vcpkg_root

    if not os.path.exists(vcpkg_root):
        print("Cloning vcpkg...")
        result = subprocess.run([
            "git", "clone", "--depth", "1",
            "https://github.com/microsoft/vcpkg.git",
            vcpkg_root
        ])
        if result.returncode != 0:
            Exit(f"Failed to clone vcpkg (exit code: {result.returncode})")

    print("Bootstrapping vcpkg...")
    if platform.system() == "Windows":
        bootstrap = os.path.join(vcpkg_root, "bootstrap-vcpkg.bat")
        result = subprocess.run([bootstrap, "-disableMetrics"], cwd=vcpkg_root, shell=True)
    else:
        bootstrap = os.path.join(vcpkg_root, "bootstrap-vcpkg.sh")
        result = subprocess.run(["bash", bootstrap, "-disableMetrics"], cwd=vcpkg_root)

    if result.returncode != 0:
        Exit(f"vcpkg bootstrap failed (exit code: {result.returncode})")

    return vcpkg_root


def _install_openssl_vcpkg(vcpkg_root, triplet):
    """Install OpenSSL via vcpkg for the given triplet."""
    vcpkg_exe = os.path.join(vcpkg_root, "vcpkg.exe" if platform.system() == "Windows" else "vcpkg")

    # Check if already installed
    installed_dir = os.path.join(vcpkg_root, "installed", triplet, "lib")
    if platform.system() == "Windows":
        check_file = os.path.join(installed_dir, "libssl.lib")
    else:
        check_file = os.path.join(installed_dir, "libssl.a")

    if os.path.exists(check_file):
        print(f"OpenSSL already installed via vcpkg ({triplet})")
        return

    print(f"Installing OpenSSL via vcpkg ({triplet})...")
    result = subprocess.run([
        vcpkg_exe, "install", f"openssl:{triplet}",
        "--recurse",
    ], cwd=vcpkg_root)
    if result.returncode != 0:
        Exit(f"vcpkg install openssl failed (exit code: {result.returncode})")

    print(f"OpenSSL installed via vcpkg ({triplet})")


def _get_vcpkg_triplet(platform_name):
    """Return the vcpkg triplet for the current platform/architecture."""
    machine = platform.machine().lower()
    if platform_name == "windows":
        return "x64-windows-static"
    elif platform_name == "macos":
        if machine in ("arm64", "aarch64"):
            return "arm64-osx"
        return "x64-osx"
    else:
        # Linux
        if machine in ("arm64", "aarch64"):
            return "arm64-linux"
        return "x64-linux"


# Main build entry point

def _build_ixwebsocket(env):
    ixws_path = "thirdparty/ixwebsocket"

    if not os.path.exists(ixws_path):
        print("Downloading IXWebSocket...")
        os.makedirs("./thirdparty", exist_ok=True)

        url = f"{BASE_URL}/{IXWEBSOCKET_VERSION}.tar.gz"
        archive_path = os.path.join("./thirdparty", f"IXWebSocket-{IXWEBSOCKET_VERSION}.tar.gz")

        download_file(url, archive_path, "IXWebSocket", IXWEBSOCKET_SHA256)
        extract_archive(archive_path, "./thirdparty")

        # GitHub strips the leading 'v' from tag names in archive directory names
        # e.g. tag v11.4.6 -> extracts as IXWebSocket-11.4.6/
        extracted_dir = os.path.join("./thirdparty", f"IXWebSocket-{IXWEBSOCKET_VERSION.lstrip('v')}")
        if os.path.exists(extracted_dir):
            os.rename(extracted_dir, ixws_path)

        os.remove(archive_path)
        print("IXWebSocket downloaded and extracted successfully.")

    platform_name = env["platform_name"]
    build_target = env["target"]
    is_android = env.get('is_android', False)
    is_wasm = env.get('is_wasm', False)

    build_dir = f"{ixws_path}/build_{platform_name}_{build_target}"

    # Expected output library path
    if platform_name == "windows":
        lib_file = os.path.join(build_dir, "Release", "ixwebsocket.lib")
    else:
        lib_file = os.path.join(build_dir, "libixwebsocket.a")

    if os.path.exists(lib_file):
        print(f"IXWebSocket already built: {lib_file}")
        return

    # Resolve OpenSSL: prefer system install, fall back to vcpkg.
    # TLS is disabled on Android/WebAssembly for now.
    use_vcpkg_toolchain = False
    vcpkg_root = None
    vcpkg_triplet = None
    openssl_root_dir = None
    use_tls = not (is_android or is_wasm)

    if use_tls:
        if platform_name == "windows":
            # Windows has no system OpenSSL - always use vcpkg
            vcpkg_root = _ensure_vcpkg()
            vcpkg_triplet = _get_vcpkg_triplet(platform_name)
            _install_openssl_vcpkg(vcpkg_root, vcpkg_triplet)
            use_vcpkg_toolchain = True
        else:
            # macOS / Linux: try system OpenSSL first
            system_ssl = _find_system_openssl(platform_name)
            if system_ssl:
                inc_dir, lib_dir = system_ssl
                # Derive the root dir (parent of include/) for CMake's FindOpenSSL
                openssl_root_dir = os.path.dirname(inc_dir)
                print(f"Using system OpenSSL: {openssl_root_dir}")
            else:
                print("System OpenSSL not found — installing via vcpkg...")
                vcpkg_root = _ensure_vcpkg()
                vcpkg_triplet = _get_vcpkg_triplet(platform_name)
                _install_openssl_vcpkg(vcpkg_root, vcpkg_triplet)
                use_vcpkg_toolchain = True

    # CMake configure
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)

    tls_label = "OpenSSL" if use_tls else "OFF"
    print(f"Building IXWebSocket for {platform_name}/{build_target} (TLS: {tls_label})...")

    cmake_args = [
        "cmake",
        f"-S{os.path.abspath(ixws_path)}",
        f"-B{os.path.abspath(build_dir)}",
        "-DBUILD_SHARED_LIBS=OFF",
        f"-DUSE_TLS={'ON' if use_tls else 'OFF'}",
        f"-DUSE_OPEN_SSL={'ON' if use_tls else 'OFF'}",
        "-DUSE_ZLIB=OFF",
        "-DCMAKE_CXX_STANDARD=20",
    ]

    if is_android:
        # Android cross-compilation via NDK CMake toolchain
        ndk_root = env.get('android_ndk_root', os.environ.get('ANDROID_NDK_ROOT', ''))
        api_level = env.get('android_api_level', 24)
        ndk_toolchain_file = os.path.join(ndk_root, "build", "cmake", "android.toolchain.cmake")

        # Must use Ninja (not Visual Studio) for Android cross-compilation.
        # The Android SDK bundles Ninja; locate it via ANDROID_HOME / ANDROID_SDK_ROOT.
        android_cmake_args = [
            "-G", "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={ndk_toolchain_file}",
            "-DANDROID_ABI=arm64-v8a",
            f"-DANDROID_PLATFORM=android-{api_level}",
            "-DANDROID_STL=c++_shared",
            "-DCMAKE_CXX_FLAGS=-std=c++20 -fPIC",
            "-DCMAKE_C_FLAGS=-fPIC",
        ]

        # Point CMake at the SDK-bundled Ninja so it doesn't fall back to MSBuild
        sdk_root = env.get('android_sdk_root',
                           os.environ.get('ANDROID_HOME',
                           os.environ.get('ANDROID_SDK_ROOT', '')))
        if sdk_root:
            ninja_dir = os.path.join(sdk_root, "cmake")
            if os.path.isdir(ninja_dir):
                # Pick the newest CMake version directory that contains ninja
                for entry in sorted(os.listdir(ninja_dir), reverse=True):
                    ninja_candidate = os.path.join(ninja_dir, entry, "bin",
                                                   "ninja.exe" if platform.system() == "Windows" else "ninja")
                    if os.path.isfile(ninja_candidate):
                        android_cmake_args.append(f"-DCMAKE_MAKE_PROGRAM={ninja_candidate}")
                        break

        cmake_args.extend(android_cmake_args)
    elif is_wasm:
        emsdk_root = env.get('emsdk_root', os.environ.get('EMSDK_ROOT', ''))
        if not emsdk_root:
            Exit("EMSDK_ROOT not configured. Cannot build IXWebSocket for WebAssembly.")
        emscripten_toolchain = os.path.join(
            emsdk_root, "upstream", "emscripten", "cmake", "Modules", "Platform", "Emscripten.cmake"
        )
        if not os.path.isfile(emscripten_toolchain):
            Exit(f"Emscripten toolchain file not found: {emscripten_toolchain}")

        ninja_program = _find_emsdk_ninja(emsdk_root)
        if not ninja_program:
            Exit("Ninja was not found in EMSDK.\n"
                 "Install/activate EMSDK tools and ensure ninja is available.\n"
                 "Expected under EMSDK_ROOT/ninja/<version>/ninja(.exe).")

        wasm_cxxflags = "-std=c++20 -fexceptions -fPIC -pthread -DPLATFORM_NAME=\\\"WebAssembly\\\""
        wasm_cflags = "-fPIC -pthread"
        wasm_link_flags = "-pthread -sALLOW_MEMORY_GROWTH=1"
        if platform_name == "wasm64":
            wasm_cxxflags += " -sWASM64=1"
            wasm_cflags += " -sWASM64=1"
            wasm_link_flags += " -sWASM64=1"

        cmake_args.extend([
            "-G", "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={emscripten_toolchain}",
            f"-DCMAKE_MAKE_PROGRAM={ninja_program}",
            f"-DCMAKE_CXX_FLAGS={wasm_cxxflags}",
            f"-DCMAKE_C_FLAGS={wasm_cflags}",
            f"-DCMAKE_EXE_LINKER_FLAGS={wasm_link_flags}",
            f"-DCMAKE_SHARED_LINKER_FLAGS={wasm_link_flags}",
        ])
    elif platform_name == "windows":
        toolchain_file = os.path.join(vcpkg_root, "scripts", "buildsystems", "vcpkg.cmake")
        cmake_args.extend([
            "-G", "Visual Studio 17 2022",
            "-A", "x64",
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
            "-DCMAKE_C_FLAGS=/MT",
            "-DCMAKE_CXX_FLAGS=/MT /std:c++20 /EHsc",
            f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}",
            f"-DVCPKG_TARGET_TRIPLET={vcpkg_triplet}",
        ])
    elif platform_name == "macos":
        cmake_args.extend([
            "-DCMAKE_CXX_FLAGS=-std=c++20",
            "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64",
        ])
        if use_vcpkg_toolchain:
            toolchain_file = os.path.join(vcpkg_root, "scripts", "buildsystems", "vcpkg.cmake")
            cmake_args.extend([
                f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}",
                f"-DVCPKG_TARGET_TRIPLET={vcpkg_triplet}",
            ])
        elif openssl_root_dir:
            cmake_args.append(f"-DOPENSSL_ROOT_DIR={openssl_root_dir}")
    else:
        # Linux
        cmake_args.extend([
            "-DCMAKE_CXX_FLAGS=-std=c++20 -fPIC",
            "-DCMAKE_C_FLAGS=-fPIC",
        ])
        if use_vcpkg_toolchain:
            toolchain_file = os.path.join(vcpkg_root, "scripts", "buildsystems", "vcpkg.cmake")
            cmake_args.extend([
                f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}",
                f"-DVCPKG_TARGET_TRIPLET={vcpkg_triplet}",
            ])
        elif openssl_root_dir:
            cmake_args.append(f"-DOPENSSL_ROOT_DIR={openssl_root_dir}")

    result = subprocess.run(cmake_args, cwd=os.getcwd())
    if result.returncode != 0:
        Exit(f"IXWebSocket CMake configuration failed (exit code: {result.returncode})")

    # CMake build
    build_args = [
        "cmake",
        "--build", os.path.abspath(build_dir),
        "--config", "Release",
        "--parallel",
    ]

    result = subprocess.run(build_args, cwd=os.getcwd())
    if result.returncode != 0:
        Exit(f"IXWebSocket build failed (exit code: {result.returncode})")

    print(f"IXWebSocket built successfully: {lib_file}")