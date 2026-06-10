# Build Script for the IDTXFlow GDExtension
import os
import platform

from  SCons.Environment import Environment
from SCons.Script import ARGUMENTS, Exit

# USD Version configuration
openusd_version = "25.11"

# Determine the target platform (may differ from host when cross-compiling)
target_platform = ARGUMENTS.get('platform', None)
if target_platform is None:
    # Auto-detect from host when not explicitly specified
    if platform.system() == "Windows":
        target_platform = "windows"
    elif platform.system() == "Darwin":
        target_platform = "macos"
    else:
        target_platform = "linux"

is_android = (target_platform == "android")

# --- Android NDK setup -----------------------------------------------------------
android_ndk_root = None
android_api_level = int(ARGUMENTS.get('android_api_level', '30'))

if is_android:
    android_ndk_root = ARGUMENTS.get('ANDROID_NDK_ROOT', os.environ.get('ANDROID_NDK_ROOT', ''))
    if not android_ndk_root or not os.path.isdir(android_ndk_root):
        Exit("ANDROID_NDK_ROOT must point to a valid Android NDK directory.\n"
             "  Pass it as:  scons platform=android ANDROID_NDK_ROOT=/path/to/ndk ...")

    # Resolve host tag for the NDK toolchain binaries
    _host_system = platform.system().lower()
    if _host_system == "windows":
        _ndk_host_tag = "windows-x86_64"
    elif _host_system == "darwin":
        _ndk_host_tag = "darwin-x86_64"
    else:
        _ndk_host_tag = "linux-x86_64"

    _ndk_toolchain = os.path.join(android_ndk_root, "toolchains", "llvm", "prebuilt", _ndk_host_tag)
    _ndk_bin = os.path.join(_ndk_toolchain, "bin")
    _exe = ".exe" if platform.system() == "Windows" else ""

    _android_target = f"aarch64-linux-android{android_api_level}"

    ndk_env_overrides = {
        "CC":     os.path.join(_ndk_bin, f"clang{_exe}"),
        "CXX":    os.path.join(_ndk_bin, f"clang++{_exe}"),
        "AR":     os.path.join(_ndk_bin, f"llvm-ar{_exe}"),
        "RANLIB": os.path.join(_ndk_bin, f"llvm-ranlib{_exe}"),
        "STRIP":  os.path.join(_ndk_bin, f"llvm-strip{_exe}"),
        "AS":     os.path.join(_ndk_bin, f"clang{_exe}"),
        "LINK":   os.path.join(_ndk_bin, f"clang++{_exe}"),
    }
else:
    ndk_env_overrides = {}

# configure the main environment to use the different tools to build all we need
_custom_tools = [
    "mdlsdk",
    "godotcpp",
    "gdextension",
    "openusd",
    "openusdextension",
    "ixwebsocket",
    "idtxflow_ext",
    "idtxflow_sdk"
    ]

if is_android:
    # On Windows the "default" tool initialises MSVC command templates
    # (/Fo, /c, /D, /I …) which are incompatible with the NDK Clang
    # toolchain.  Use gcc-style tools instead so that SCons emits the
    # correct -o / -c / -D / -I flags for clang.
    env = Environment(
        ENV=os.environ.copy(),
        tools=["gcc", "g++", "gnulink", "ar", "gas"] + _custom_tools,
        toolpath=["scons"],
        PATH=os.environ.get("PATH", ""),
        **ndk_env_overrides,
    )
    env.Append(CCFLAGS=[f"--target={_android_target}", "-march=armv8-a"])
    env.Append(LINKFLAGS=[f"--target={_android_target}", "-march=armv8-a"])
else:
    env = Environment(
        ENV=os.environ.copy(),
        tools=["default"] + _custom_tools,
        toolpath=["scons"],
        MSVC_VERSION='14.3',
        PATH=os.environ.get("PATH", ""),
        **ndk_env_overrides,
    )

# Store the target platform (decoupled from host)
env["PLATFORM"] = target_platform
env['platform_name'] = target_platform

# Architecture
if is_android:
    # Android on Meta Quest / phones is always arm64
    env['arch'] = ARGUMENTS.get('arch', 'arm64')
else:
    arch = platform.machine().lower()
    if arch in ("aarch64", "arm64"):
        arch = "arm64"
    env['arch'] = ARGUMENTS.get('arch', arch)

# default build target should be debug
env['target'] = ARGUMENTS.get('target', 'template_debug')

# Store Android-specific settings in the environment for sub-tools
if is_android:
    env['android_ndk_root'] = android_ndk_root
    env['android_api_level'] = android_api_level
    env['is_android'] = True
else:
    env['is_android'] = False

# Compiler flags
if is_android:
    # Android NDK Clang: Enable C++20
    env.Append(CXXFLAGS=['-std=c++20'])
elif platform.system() == "Windows" and (env["CXX"] == "cl" or env["CC"] == "cl"):
    # MSVC: Enable C++20
    env.Append(CXXFLAGS=['/std:c++20'])
else:
    # GCC/Clang: Enable C++20
    env.Append(CXXFLAGS=['-std=c++20'])

env['openusd_version'] = openusd_version

# download and build IXWebSocket from source as a static library
env.BuildIXWebSocket()
# download and build openUSD from source without python support, as we don't need it and it will speed up the build process significantly
env.BuildOpenUSD(with_python_support=False)
env.BuildOpenUSD(with_python_support=True)  # with python support, to be able to generate the usd plugin code
# generate the openUSD extension (plugin) code
env.GenerateUsdExtensionCode()
# compile the openUSD extension into it's library
env.BuildUsdExtension()
# download NVIDIA's mdlSdk — not available on Android
if not is_android:
    env.DownloadMdlSdk()
# download and build the Godot C++ bindings
env = env.BuildGodotCPP()
# Build the extension bootstrap static library (for dependent third-party extensions)
env.BuildExtBootstrapLib()
# finally build the GDExtension itself, which will link against the previously built OpenUSD and Godot C++ bindings
env.BuildGdExtension()
# with the GDExtension built, we can grab everything that is required to form an IDTXFlowGodotExtension SDK to implement
# an extension of this very GDExtension
env.ComposeIdtxFlowGodotSDK()