"""SCons tool: mbedtls (Android TLS backend)

Cross-compiles MbedTLS as static libraries for Android so IXWebSocket can be
built with a TLS backend there (desktop keeps using OpenSSL via the OpenSSL
locator). No-op on non-Android platforms.

Registers the SCons env method ``BuildMbedTLSV2``.

MbedTLS 3.5.2 is pinned deliberately: it is the last 3.x release whose GitHub
tag tarball is self-contained. From 3.6 onward the top-level CMake build hard-
requires the ``framework/`` git submodule (absent from tag tarballs), which the
download pipeline here cannot fetch.
"""
import os

from scons.core import cmake_runner
from scons.core.fetcher import ThirdPartySource
from scons.platforms import android as android_platform


MBEDTLS_VERSION = "mbedtls-3.5.2"   # GitHub tag
MBEDTLS_SHA256  = "eedecc468b3f8d052ef05a9d42bf63f04c8a1c50d1c5a94c251c681365a2c723"
BASE_URL        = "https://github.com/Mbed-TLS/mbedtls/archive/refs/tags"


def generate(env):
    env.AddMethod(_build_mbedtls, "BuildMbedTLSV2")


def exists(env):
    return True


def _build_mbedtls(env):
    profile  = env["profile"]
    platform = profile.platform
    paths    = profile.paths

    # Desktop builds use OpenSSL; MbedTLS is only needed to give Android a TLS
    # backend. Nothing to do elsewhere.
    if not platform.is_android:
        return

    # ---- Fetch source -------------------------------------------------------
    src = ThirdPartySource(
        name="MbedTLS",
        url=f"{BASE_URL}/{MBEDTLS_VERSION}.tar.gz",
        sha256=MBEDTLS_SHA256,
        archive_filename=f"{MBEDTLS_VERSION}.tar.gz",
        extract_root=paths.thirdparty,
        install_dir=paths.mbedtls,
        # Archive extracts to mbedtls-<tag>/, e.g. mbedtls-mbedtls-3.5.2/.
        extracted_subdir=f"mbedtls-{MBEDTLS_VERSION}",
    )
    src.ensure()

    # ---- Already built? -----------------------------------------------------
    install_dir = os.path.abspath(paths.mbedtls_install)
    lib_file = os.path.join(install_dir, "lib", "libmbedtls.a")
    if os.path.exists(lib_file):
        print(f"MbedTLS already built: {lib_file}")
        return

    # ---- Configure + build + install static libs ---------------------------
    ndk_root  = env.get("android_ndk_root", os.environ.get("ANDROID_NDK_ROOT", ""))
    # Match the extension's own API level (SConstruct default 30) so the whole
    # Android build stays on one toolchain/ABI.
    api_level = int(env.get("android_api_level", 30))

    cmake_args = android_platform.cmake_toolchain_args(ndk_root, api_level) + [
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_INSTALL_PREFIX={install_dir}",
        "-DUSE_SHARED_MBEDTLS_LIBRARY=OFF",
        "-DUSE_STATIC_MBEDTLS_LIBRARY=ON",
        "-DENABLE_TESTING=OFF",
        "-DENABLE_PROGRAMS=OFF",
        # Use the pre-generated PSA sources shipped in the tarball instead of
        # regenerating them (which would need Python + jinja2 + the framework).
        "-DGEN_FILES=OFF",
        # NDK r29's clang is newer than MbedTLS 3.5.2 expects; don't let fresh
        # warnings become -Werror build failures.
        "-DMBEDTLS_FATAL_WARNINGS=OFF",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        "-DCMAKE_C_FLAGS=-fPIC",
    ]

    print("Cross-compiling MbedTLS for android (arm64, static)...")
    cmake_runner.configure(paths.mbedtls, paths.mbedtls_build, cmake_args, "MbedTLS")
    cmake_runner.build(paths.mbedtls_build, "MbedTLS")
    cmake_runner.install(paths.mbedtls_build, "MbedTLS")

    if not os.path.exists(lib_file):
        env.Exit(f"MbedTLS build completed but lib is missing:\n  {lib_file}")
    print(f"MbedTLS for android built successfully: {lib_file}")
