"""SCons tool: godotcpp (refactored)

Builds the godot-cpp library.

Registers the SCons env method ``BuildGodotCPPV2``.
"""
import os

from scons.core.fetcher import ThirdPartySource
from scons.platforms import android as android_platform


GODOTCPP_VERSION = "godot-4.5-stable"
GODOTCPP_SHA256  = "ac78539c0042554c494ea419549d2de88758d448721aeb0e5d41129aa87e339c"
BASE_URL         = "https://github.com/godotengine/godot-cpp/archive/refs/tags"


def generate(env):
    env.AddMethod(_build_godot_cpp, "BuildGodotCPPV2")


def exists(env):
    return True


def _build_godot_cpp(env):
    profile  = env["profile"]
    platform = profile.platform
    paths    = profile.paths

    src = ThirdPartySource(
        name="godot-cpp",
        url=f"{BASE_URL}/{GODOTCPP_VERSION}.tar.gz",
        sha256=GODOTCPP_SHA256,
        archive_filename=f"{GODOTCPP_VERSION}.tar.gz",
        extract_root=paths.thirdparty,
        install_dir=paths.godot_cpp,
        extracted_subdir=f"godot-cpp-{GODOTCPP_VERSION}",
    )
    src.ensure()

    print("Building godot-cpp...")
    env["use_exceptions"] = "yes"
    env["use_rtti"]       = "yes"
    env["use_threads"]    = "yes"

    if platform.is_android:
        env["platform"] = "android"
        env["android_api_level"] = str(env.get("android_api_level", 24))
        # Force godot-cpp to read ANDROID_NDK_ROOT instead of constructing a
        # path from ANDROID_HOME + a hardcoded NDK version.
        ndk_root = env.get("android_ndk_root", os.environ.get("ANDROID_NDK_ROOT", ""))
        os.environ["ANDROID_NDK_ROOT"] = ndk_root
        env["ENV"]["ANDROID_NDK_ROOT"] = ndk_root
        env["ANDROID_HOME"] = ""
        android_platform.clear_android_home_for_godotcpp()

    return env.SConscript(f"{paths.godot_cpp}/SConstruct", exports=["env"])