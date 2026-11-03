# Build Script for the IDTXFlow GDExtension
import glob
import os
import platform

from  SCons.Environment import Environment
from SCons.Script import ARGUMENTS, Exit

# USD Version and path configuration
openusd_version = "26.05"
shared_thirdparty_root = "./thirdparty"

# allow developer to overwrite those paths
try:
    import custom
except ImportError:
    custom = None

if custom:
    openusd_version = getattr(custom, "OPENUSD_VERSION", openusd_version)
    shared_thirdparty_root = getattr(custom, "SHARED_THIRDPARTY_ROOT", shared_thirdparty_root)

usd_src = f"{shared_thirdparty_root}/openusd-{openusd_version}-src"

# Target platform: the host by default, or platform=android to cross-compile
target_platform = ARGUMENTS.get('platform', None)
if target_platform is None:
    if platform.system() == "Windows":
        target_platform = "windows"
    elif platform.system() == "Darwin":
        target_platform = "macos"
    else:
        target_platform = "linux"
is_android = target_platform == "android"

# the Android build gets its own OpenUSD install next to the desktop one
usd_root = f"{shared_thirdparty_root}/openusd-{openusd_version}" + ("-android" if is_android else "")

android_env_overrides = {}
android_api_level = int(ARGUMENTS.get('android_api_level', '30'))
android_ndk_root = None

if is_android:
    android_ndk_root = ARGUMENTS.get('ANDROID_NDK_ROOT', os.environ.get('ANDROID_NDK_ROOT', ''))
    if not android_ndk_root or not os.path.isdir(android_ndk_root):
        Exit("ANDROID_NDK_ROOT must point to a valid Android NDK directory.\n"
             "  Pass it as:  scons platform=android ANDROID_NDK_ROOT=/path/to/ndk ...")
    # the NDK ships one prebuilt toolchain per host (windows-x86_64, linux-x86_64, darwin-x86_64 / darwin-arm64 ...)
    _prebuilt = glob.glob(os.path.join(android_ndk_root, "toolchains", "llvm", "prebuilt", "*", "bin"))
    if not _prebuilt:
        Exit(f"No prebuilt LLVM toolchain found in {android_ndk_root}/toolchains/llvm/prebuilt")
    _ndk_bin = _prebuilt[0]
    _exe = ".exe" if platform.system() == "Windows" else ""
    android_env_overrides = {
        "CC": os.path.join(_ndk_bin, f"clang{_exe}"),
        "CXX": os.path.join(_ndk_bin, f"clang++{_exe}"),
        "AR": os.path.join(_ndk_bin, f"llvm-ar{_exe}"),
        "RANLIB": os.path.join(_ndk_bin, f"llvm-ranlib{_exe}"),
        "STRIP": os.path.join(_ndk_bin, f"llvm-strip{_exe}"),
        "AS": os.path.join(_ndk_bin, f"clang{_exe}"),
        "LINK": os.path.join(_ndk_bin, f"clang++{_exe}"),
    }

# configure the main environment to use the different tools to build all we need
env = Environment(
    ENV=os.environ.copy(),
    tools=[
    	# on Windows "default" sets up MSVC command templates (/Fo, /c ...) that the NDK clang rejects
    	*(["gcc", "g++", "gnulink", "ar", "gas"] if is_android else ["default"]),
    	"mdlsdk",
    	"godotcpp",
    	"gdextension",
    	"openusd",
    	"openusdextension",
    	"ixwebsocket",
    	"protobuf",
    	"idtxflow_ext",
    	"idtxflow_sdk"
    ],
    toolpath=["scons"],
    MSVC_VERSION='14.3',
    OPENUSD_VERSION=openusd_version,
    OPENUSD_PATH=usd_root,
    OPENUSD_HOST_PATH=f"{shared_thirdparty_root}/openusd-{openusd_version}",
    OPENUSD_SRC_PATH=usd_src,
    # oneTBB is cross-compiled separately for Android (see scons/openusd.py)
    ONETBB_ANDROID_PATH=f"{shared_thirdparty_root}/onetbb-android",
    PATH=os.environ.get("PATH", ""),
    **android_env_overrides,
)

env["PLATFORM"] = target_platform
env['platform_name'] = target_platform
env['is_android'] = is_android
if is_android:
    env['android_ndk_root'] = android_ndk_root
    env['android_api_level'] = android_api_level
    _android_target = f"aarch64-linux-android{android_api_level}"
    env.Append(CCFLAGS=[f"--target={_android_target}", "-march=armv8-a"])
    env.Append(LINKFLAGS=[f"--target={_android_target}", "-march=armv8-a"])
    env['arch'] = ARGUMENTS.get('arch', 'arm64')
else:
    arch = platform.machine().lower()
    if arch in ("aarch64", "arm64"):
        arch = "arm64"
    env['arch'] = ARGUMENTS.get('arch', arch )
# default build target should be debug
env['target'] = ARGUMENTS.get('target', 'template_debug')

if not is_android and platform.system() == "Windows" and (env["CXX"] == "cl" or env["CC"] == "cl"):
    # MSVC: Enable C++20
    env.Append(CXXFLAGS=['/std:c++20'])
else:
    # GCC/Clang: Enable C++20
    env.Append(CXXFLAGS=['-std=c++20'])

# download and build IXWebSocket from source as a static library
env.BuildIXWebSocket()
# download and build protobuf from source (static lib + protoc), then generate the
# C++ sources for the idtxcore.* wire messages used by the collaboration client.
# Generated sources land in shared/src/idtxflow/net/proto_gen (compiled into the
# extension) with headers alongside them (added to the include path in gdextension.py).
env.BuildProtobuf()
env.GenerateProtoSources("shared/proto/idtxcore", "shared/src/idtxflow/net/proto_gen")
# download and build openUSD from source without python support, as we don't need it and it will speed up the build process significantly
env.BuildOpenUSD(with_python_support=False)
# with python support, to be able to generate the usd plugin code (always a host/desktop build)
env.BuildOpenUSD(with_python_support=True)
# generate the openUSD extension (plugin) code
env.GenerateUsdExtensionCode()
# compile the openUSD extension into it's library
env.BuildUsdExtension()
# download NVIDIA's mdlSdk
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
