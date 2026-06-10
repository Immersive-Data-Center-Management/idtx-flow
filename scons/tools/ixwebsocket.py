"""SCons tool: ixwebsocket (refactored)

Builds the IXWebSocket library via CMake. Uses the consolidated OpenSSL
locator (with vcpkg fallback) instead of duplicating the probe.

Registers the SCons env method ``BuildIXWebSocketV2``.
"""
import os

from scons.core import cmake_runner, openssl_locator, vcpkg
from scons.core.fetcher import ThirdPartySource
from scons.platforms import android as android_platform


IXWEBSOCKET_VERSION = "v11.4.6"
IXWEBSOCKET_SHA256  = "c024334f8e45980836c67008979a884d6dcc5ef067dd2eb1fa7241f4c17ddc32"
BASE_URL            = "https://github.com/machinezone/IXWebSocket/archive/refs/tags"


def generate(env):
    env.AddMethod(_build_ixwebsocket, "BuildIXWebSocketV2")


def exists(env):
    return True


def _build_ixwebsocket(env):
    profile  = env["profile"]
    platform = profile.platform
    paths    = profile.paths

    # ---- Fetch source -------------------------------------------------------
    src = ThirdPartySource(
        name="IXWebSocket",
        url=f"{BASE_URL}/{IXWEBSOCKET_VERSION}.tar.gz",
        sha256=IXWEBSOCKET_SHA256,
        archive_filename=f"IXWebSocket-{IXWEBSOCKET_VERSION}.tar.gz",
        extract_root=paths.thirdparty,
        install_dir=paths.ixwebsocket,
        # GitHub strips the leading 'v' from tag names in archive directory
        # names. Tag v11.4.6 extracts as IXWebSocket-11.4.6/.
        extracted_subdir=f"IXWebSocket-{IXWEBSOCKET_VERSION.lstrip('v')}",
    )
    src.ensure()

    # ---- Already built? -----------------------------------------------------
    build_dir = paths.ixwebsocket_build
    if platform.is_windows:
        lib_file = os.path.join(build_dir, "Release", "ixwebsocket.lib")
    else:
        lib_file = os.path.join(build_dir, "libixwebsocket.a")

    if os.path.exists(lib_file):
        print(f"IXWebSocket already built: {lib_file}")
        return

    # ---- Resolve OpenSSL (TLS disabled on Android) --------------------------
    use_tls = not platform.is_android
    openssl = openssl_locator.find_openssl(platform) if use_tls else None

    # ---- Build CMake configure args ----------------------------------------
    cmake_args = [
        "-DBUILD_SHARED_LIBS=OFF",
        f"-DUSE_TLS={'ON' if use_tls else 'OFF'}",
        f"-DUSE_OPEN_SSL={'ON' if use_tls else 'OFF'}",
        "-DUSE_ZLIB=OFF",
        "-DCMAKE_CXX_STANDARD=20",
    ]

    if platform.is_android:
        ndk_root  = env.get("android_ndk_root", os.environ.get("ANDROID_NDK_ROOT", ""))
        api_level = env.get("android_api_level", 24)
        cmake_args.extend(android_platform.cmake_toolchain_args(ndk_root, api_level))
        cmake_args.extend([
            "-DCMAKE_CXX_FLAGS=-std=c++20 -fPIC",
            "-DCMAKE_C_FLAGS=-fPIC",
        ])
    elif platform.is_windows:
        cmake_args.extend([
            "-G", "Visual Studio 17 2022",
            "-A", "x64",
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
            "-DCMAKE_C_FLAGS=/MT",
            "-DCMAKE_CXX_FLAGS=/MT /std:c++20 /EHsc",
        ])
        if openssl and openssl.is_vcpkg:
            cmake_args.extend([
                f"-DCMAKE_TOOLCHAIN_FILE={vcpkg.toolchain_file(openssl.vcpkg_root)}",
                f"-DVCPKG_TARGET_TRIPLET={openssl.vcpkg_triplet}",
            ])
    elif platform.is_macos:
        cmake_args.extend([
            "-DCMAKE_CXX_FLAGS=-std=c++20",
            "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64",
        ])
        if openssl and openssl.is_vcpkg:
            cmake_args.extend([
                f"-DCMAKE_TOOLCHAIN_FILE={vcpkg.toolchain_file(openssl.vcpkg_root)}",
                f"-DVCPKG_TARGET_TRIPLET={openssl.vcpkg_triplet}",
            ])
        elif openssl:
            cmake_args.append(f"-DOPENSSL_ROOT_DIR={openssl.cmake_root_dir}")
    else:
        # Linux
        cmake_args.extend([
            "-DCMAKE_CXX_FLAGS=-std=c++20 -fPIC",
            "-DCMAKE_C_FLAGS=-fPIC",
        ])
        if openssl and openssl.is_vcpkg:
            cmake_args.extend([
                f"-DCMAKE_TOOLCHAIN_FILE={vcpkg.toolchain_file(openssl.vcpkg_root)}",
                f"-DVCPKG_TARGET_TRIPLET={openssl.vcpkg_triplet}",
            ])
        elif openssl:
            cmake_args.append(f"-DOPENSSL_ROOT_DIR={openssl.cmake_root_dir}")

    tls_label = "OpenSSL" if use_tls else "OFF"
    print(f"Building IXWebSocket for {platform.name}/{platform.target} (TLS: {tls_label})...")

    cmake_runner.configure(paths.ixwebsocket, build_dir, cmake_args, "IXWebSocket")
    cmake_runner.build(build_dir, "IXWebSocket")

    print(f"IXWebSocket built successfully: {lib_file}")