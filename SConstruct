# Build Script for the IDTXFlow GDExtension
import os
import platform

from  SCons.Environment import Environment
from SCons.Script import ARGUMENTS

# USD Version configuration
openusd_version = "25.11"

# configure the main environment to use the different tools to build all we need
env = Environment(
    ENV=os.environ.copy(),
    tools=[
    	"default",
    	"mdlsdk",
    	"godotcpp",
    	"gdextension",
    	"openusd",
    	"openusdextension",
    	"ixwebsocket",
    	"idtxflow_ext",
    	"idtxflow_sdk"
    ],
    toolpath=["scons"],
    MSVC_VERSION='14.3',
    PATH=os.environ.get("PATH", "")
)

if platform.system() == "Windows":
    env["PLATFORM"] = "windows"
elif platform.system() == "Darwin":
    env["PLATFORM"] = "macos"
else:
    env["PLATFORM"] = "linux"

env['platform_name'] = env["PLATFORM"]
arch = platform.machine().lower()
if arch in ("aarch64", "arm64"):
    arch = "arm64"
env['arch'] = ARGUMENTS.get('arch', arch )
# default build target should be debug
env['target'] = ARGUMENTS.get('target', 'template_debug')

if platform.system() == "Windows" and (env["CXX"] == "cl" or env["CC"] == "cl"):
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