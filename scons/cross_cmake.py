"""
Shared helpers for cross-compiling the CMake based dependencies (oneTBB, OpenUSD, IXWebSocket, protobuf) for Android.
"""
import glob
import os
import platform
import shutil

from SCons.Script import Exit


def find_ninja(env):
    """Ninja is required for the Android build (the Visual Studio generator cannot drive the NDK compilers).
    Prefer PATH, then the copies bundled with the Android SDK and vcpkg."""
    exe = "ninja.exe" if platform.system() == "Windows" else "ninja"
    found = shutil.which(exe)
    if found:
        return found

    # read from env["ENV"]: godotcpp.py clears ANDROID_HOME for the godot-cpp build
    sdk_root = env["ENV"].get("ANDROID_HOME") or env["ENV"].get("ANDROID_SDK_ROOT") or ""
    patterns = [
        os.path.join(sdk_root, "cmake", "*", "bin") if sdk_root else None,
        os.path.join("thirdparty", "vcpkg", "downloads", "tools", "ninja-*"),
    ]
    for pattern in filter(None, patterns):
        for directory in sorted(glob.glob(pattern), reverse=True):  # newest version first
            candidate = os.path.join(directory, exe)
            if os.path.isfile(candidate):
                return os.path.abspath(candidate)

    Exit("Ninja is required for Android builds but was not found.\n"
         "  Put ninja on PATH (or install CMake through the Android SDK manager).")


def cmake_cross_args(env):
    """CMake configure arguments (generator + NDK toolchain) for the Android target."""
    ndk_root = env["android_ndk_root"]
    return [
        "-G", "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={find_ninja(env)}",
        f"-DCMAKE_TOOLCHAIN_FILE={os.path.join(ndk_root, 'build', 'cmake', 'android.toolchain.cmake')}",
        "-DANDROID_ABI=arm64-v8a",
        f"-DANDROID_PLATFORM=android-{env['android_api_level']}",
        "-DANDROID_STL=c++_shared",
    ]
