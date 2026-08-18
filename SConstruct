"""
Build Script for IDTXFlow.

This is the core script delegating the actual work to the different SCons tools.

The pipeline (top to bottom):
    1. Download/clone third-party dependencies and tools
       - godotcpp, OpenUSD, IXWebSocket, MDL SDK
    2. Compile each third-party dependency (where required) from source
    3. Compile the IDTXFlow GDExtension itself
    4. Compile a small bootstrap library so downstream extensions can link
       against IDTXFlow
    5. Compose an IDTXFlow SDK package (headers + libs) for downstream use

The build is fully driven by a precomputed ``BuildProfile`` constructed at
the top of this file:

    profile = build_profile(ARGUMENTS, openusd_version=OPENUSD_VERSION)

This profile is the *single* source of truth for what flags, link
libraries, install names, NDK toolchain args, and library naming this
build will use. Tools never decide flags themselves — they read
``env["profile"].cxx_flags`` (etc.) and pass each section through
``apply_profile_section(env, ...)``.

That keeps three layers cleanly separated:

  * **profile**     — pure data; inspecting it tells you everything
                      that will be appended to the env.
  * **applicator**  — mechanical; ``env.Append(**section)``, no
                      decisions.
  * **side-effecting helpers** — ``cmake_runner``, ``openssl_locator``,
                                  ``vcpkg``, ``msvc_env``, ``run`` —
                                  procedural, with explicit call sites.
"""

import os

from SCons.Environment import Environment
from SCons.Script import ARGUMENTS

from scons.core.apply import apply_profile_section
from scons.core.profile import build_profile

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
# USD Version and path configuration
openusd_version = "26.08"
shared_thirdparty_root = "./thirdparty"

# allow developer to overwrite those paths
try:
    import custom
except ImportError:
    custom = None

if custom:
    openusd_version = getattr(custom, "OPENUSD_VERSION", openusd_version)
    shared_thirdparty_root = getattr(custom, "SHARED_THIRDPARTY_ROOT", shared_thirdparty_root)

usd_root = f"{shared_thirdparty_root}/openusd-{openusd_version}"
usd_src = f"{shared_thirdparty_root}/openusd-{openusd_version}-src"

# Construct the profile once. This validates ANDROID_NDK_ROOT when
# targeting Android (calls Exit on failure) and freezes every flag and
# path decision the build will make.
profile  = build_profile(ARGUMENTS, openusd_version=openusd_version)
platform = profile.platform
paths    = profile.paths


# ---------------------------------------------------------------------------
# SCons Environment
# ---------------------------------------------------------------------------

# Refactored tools live in scons/tools and register V2 method names so
# they don't collide with the legacy methods in scons/.
SCONS_TOOLS = [
    "godotcpp",
    "mdlsdk",
    "ixwebsocket",
    "openusd",
    "usd_extension",
    "ext_bootstrap",
    "gdextension",
    "sdk_composer",
]

if platform.is_android:
    # On Windows the "default" tool initialises MSVC command templates
    # (/Fo /c /D /I) that are incompatible with the NDK Clang toolchain.
    # Use gcc-style tools so SCons emits -o / -c / -D / -I instead.
    env = Environment(
        ENV=os.environ.copy(),
        tools=["gcc", "g++", "gnulink", "ar", "gas"] + SCONS_TOOLS,
        toolpath=["scons/tools"],
        PATH=os.environ.get("PATH", ""),
        OPENUSD_VERSION=openusd_version,
        OPENUSD_PATH=usd_root,
        OPENUSD_SRC_PATH=usd_src,
        **profile.scons_env_overrides,   # NDK CC/CXX/AR/RANLIB/STRIP/AS/LINK
    )
    # Stash the NDK info so tools that shell out to CMake (ixwebsocket,
    # openusd-android, oneTBB-android) can reconstruct the toolchain args.
    import scons.platforms.android as android_platform
    env["android_ndk_root"]  = android_platform.resolve_ndk_root(ARGUMENTS)
    env["android_api_level"] = int(ARGUMENTS.get("android_api_level", "30"))
else:
    env = Environment(
        ENV=os.environ.copy(),
        tools=["default"] + SCONS_TOOLS,
        toolpath=["scons/tools"],
        MSVC_VERSION="14.3",
        OPENUSD_VERSION=openusd_version,
        OPENUSD_PATH=usd_root,
        OPENUSD_SRC_PATH=usd_src,
        PATH=os.environ.get("PATH", ""),
    )


# ---------------------------------------------------------------------------
# Stash the profile on the env  (every tool reads from env["profile"])
# ---------------------------------------------------------------------------

env["profile"] = profile

# Back-compat keys — godot-cpp's own SConstruct expects these.
env["PLATFORM"]      = platform.name
env["platform_name"] = platform.name
env["target"]        = platform.target
env["arch"]          = platform.arch


# ---------------------------------------------------------------------------
# Apply the profile's flag sections to the root env once.
#
# Each tool will *also* apply the relevant sections to its own Cloned
# env (Clone() captures the env state at clone time, so flags set on the
# root env after a tool runs Clone() do not propagate). Applying here
# covers anything that compiles directly off the root env.
# ---------------------------------------------------------------------------

apply_profile_section(env, profile.cxx_flags)
apply_profile_section(env, profile.msvc_extras)


# ---------------------------------------------------------------------------
# Build pipeline
# ---------------------------------------------------------------------------

# 1. Third-party libraries
env.BuildIXWebSocketV2()
env.BuildOpenUSDV2(with_python_support=False)
env.BuildOpenUSDV2(with_python_support=True)   # needed for usdGenSchema

# 2. IDTX USD plugin (codegen + shared lib)
env.GenerateUsdExtensionCodeV2()
env.BuildUsdExtensionV2()

# 3. NVIDIA MDL SDK — desktop only
if not platform.is_android:
    env.DownloadMdlSdkV2()

# 4. godot-cpp (returns a *new* env — replaces the local one for the rest)
env = env.BuildGodotCPPV2()

# 5. Bootstrap static lib for downstream extensions
env.BuildExtBootstrapLibV2()

# 6. The GDExtension itself, then compose the SDK
env.BuildGdExtensionV2()
env.ComposeIdtxFlowGodotSDKV2()
