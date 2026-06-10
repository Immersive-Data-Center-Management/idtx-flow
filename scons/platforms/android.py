"""Android-specific build helpers.

Bundles all the cross-cutting Android knowledge that's needed by more than
one tool:

  - NDK root resolution + host tag + toolchain bin paths
  - SCons env overrides (CC/CXX/AR/...) so SCons drives NDK clang
  - Android-specific compile/link flags (--target=, -march=, link libs,
    relro/now)
  - CMake toolchain-file args + Ninja-from-Android-SDK discovery (used by
    the IXWebSocket and OpenUSD CMake builds)

OpenUSD-Android specifics that are *only* used by the openusd tool — the
five source patches and the CMake-direct USD backend — stay inside
``tools/openusd_v2.py``.  They're not generic Android infrastructure;
they're "how to coerce OpenUSD into building on Android".
"""
import os
import platform
from SCons.Script import Exit


# ---------------------------------------------------------------------------
# NDK location
# ---------------------------------------------------------------------------

def resolve_ndk_root(args) -> str:
    """Return a validated path to the Android NDK, or call Exit."""
    ndk = args.get("ANDROID_NDK_ROOT", os.environ.get("ANDROID_NDK_ROOT", ""))
    if not ndk or not os.path.isdir(ndk):
        Exit("ANDROID_NDK_ROOT must point to a valid Android NDK directory.\n"
             "  Pass it as:  scons platform=android ANDROID_NDK_ROOT=/path/to/ndk ...")
    return ndk


def host_tag() -> str:
    sys_name = platform.system().lower()
    if sys_name == "windows":
        return "windows-x86_64"
    if sys_name == "darwin":
        return "darwin-x86_64"
    return "linux-x86_64"


def toolchain_bin(ndk_root: str) -> str:
    return os.path.join(ndk_root, "toolchains", "llvm", "prebuilt", host_tag(), "bin")


def cmake_toolchain_file(ndk_root: str) -> str:
    return os.path.join(ndk_root, "build", "cmake", "android.toolchain.cmake")


# ---------------------------------------------------------------------------
# SCons env construction
# ---------------------------------------------------------------------------

def scons_env_overrides(ndk_root: str) -> dict:
    """CC/CXX/AR/... overrides so SCons emits the right compiler invocations."""
    bin_dir = toolchain_bin(ndk_root)
    exe = ".exe" if platform.system() == "Windows" else ""
    return {
        "CC":     os.path.join(bin_dir, f"clang{exe}"),
        "CXX":    os.path.join(bin_dir, f"clang++{exe}"),
        "AR":     os.path.join(bin_dir, f"llvm-ar{exe}"),
        "RANLIB": os.path.join(bin_dir, f"llvm-ranlib{exe}"),
        "STRIP":  os.path.join(bin_dir, f"llvm-strip{exe}"),
        "AS":     os.path.join(bin_dir, f"clang{exe}"),
        "LINK":   os.path.join(bin_dir, f"clang++{exe}"),
    }


def apply_compile_flags(env, api_level: int) -> None:
    """Append the Android target triple + arch flags to a SCons env."""
    target_triple = f"aarch64-linux-android{api_level}"
    env.Append(CCFLAGS=[f"--target={target_triple}", "-march=armv8-a"])
    env.Append(LINKFLAGS=[f"--target={target_triple}", "-march=armv8-a"])


# ---------------------------------------------------------------------------
# CMake cross-compilation
# ---------------------------------------------------------------------------

def cmake_toolchain_args(ndk_root: str, api_level: int) -> list:
    """The args passed to ``cmake -S ... -B ...`` when cross-compiling for Android."""
    args = [
        "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={cmake_toolchain_file(ndk_root)}",
        "-DANDROID_ABI=arm64-v8a",
        f"-DANDROID_PLATFORM=android-{api_level}",
        "-DANDROID_STL=c++_shared",
    ]
    ninja = locate_sdk_ninja()
    if ninja:
        args.append(f"-DCMAKE_MAKE_PROGRAM={ninja}")
    return args


def locate_sdk_ninja() -> str:
    """Locate the Ninja bundled with the Android SDK.

    Returns the absolute path or an empty string. CMake on Windows defaults
    to MSBuild without this hint, which doesn't work for cross-compilation.
    """
    sdk_root = (os.environ.get("ANDROID_HOME")
                or os.environ.get("ANDROID_SDK_ROOT") or "")
    if not sdk_root:
        return ""

    cmake_dir = os.path.join(sdk_root, "cmake")
    if not os.path.isdir(cmake_dir):
        return ""

    exe = "ninja.exe" if platform.system() == "Windows" else "ninja"
    for entry in sorted(os.listdir(cmake_dir), reverse=True):
        ninja_candidate = os.path.join(cmake_dir, entry, "bin", exe)
        if os.path.isfile(ninja_candidate):
            return ninja_candidate
    return ""


def clear_android_home_for_godotcpp() -> None:
    """Delete ANDROID_HOME / ANDROID_SDK_ROOT from the process environment.

    godot-cpp's get_android_ndk_root() builds an NDK path from
    ANDROID_HOME + a hardcoded ndk_version (e.g. 28.1.13356709).  When a
    different NDK version is in use we must bypass that logic so godot-cpp
    falls through to ANDROID_NDK_ROOT instead.  Callers should set
    ANDROID_NDK_ROOT explicitly *before* calling this.
    """
    os.environ.pop("ANDROID_HOME", None)
    os.environ.pop("ANDROID_SDK_ROOT", None)