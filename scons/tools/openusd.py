"""SCons tool: openusd (refactored)

Two backends:
  - Desktop (Windows/Linux/macOS): delegate to OpenUSD's upstream
    ``build_usd.py`` Python script.

Both backends are kept in this single file because they are *only* used
by this tool. Splitting them out would just add files without reducing
duplication.

Registers the SCons env method ``BuildOpenUSDV2``.
"""
import os
import platform as sys_platform
import subprocess

from SCons.Script import Exit

from scons.core import msvc_env
from scons.core.run import run_or_exit
from scons.core.platform_info import PlatformInfo


def generate(env):
    env.AddMethod(_build_openusd, "BuildOpenUSD")


def exists(env):
    return True


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def _build_openusd(env, with_python_support: bool = False):
    profile  = env["profile"]
    platform = profile.platform if not with_python_support else PlatformInfo.host()
    paths    = profile.paths

    # Clone OpenUSD source if missing
    if not os.path.exists(paths.openusd_src):
        print("Cloning openUSD...")
        run_or_exit(
            ["git", "clone", "-b", f"v{paths.openusd_version}",
             "--recursive", "--depth", "2",
             "https://github.com/PixarAnimationStudios/OpenUSD.git",
             paths.openusd_src],
            "openUSD git clone",
        )

    # Pick the install path for this build variant
    # with_python_support
    if with_python_support:
        install_path = paths.openusd_python
    else:
        install_path = paths.openusd_install

    # Already-built probe — short-circuits subsequent invocations
    # when python support is requestes always check against the host-system name
    # as we build the python variant always for the host system
    if platform.is_windows or (with_python_support and sys_platform.system() == "Windows"):
        marker = f"{install_path}/lib/usd_ms.dll"
    elif platform.is_macos or (with_python_support and sys_platform.system() == "Darwin"):
        marker = f"{install_path}/lib/libusd_ms.dylib"
    else:
        marker = f"{install_path}/lib/libusd_ms.so"

    if os.path.exists(marker):
        print(f"openUSD exist and will be reu-used from {install_path}")
        return

    _build_for_desktop(env, paths.openusd_src, install_path,
                        platform, with_python_support)


# ---------------------------------------------------------------------------
# Desktop backend (build_usd.py)
# ---------------------------------------------------------------------------

def _build_for_desktop(env, source_path, install_path, platform, with_python_support):
    print("Building openUSD (desktop)...")
    build_env = {}

    if platform.is_windows:
        # Capture the MSVC env for the spawned Python build process
        build_env.update(msvc_env.get_msvc_env())
    else:
        build_env["PATH"] = os.environ.get("PATH", "")

    # Resolve a Python interpreter (python3 preferred)
    python_cmd = "python3"
    try:
        subprocess.run([python_cmd, "--version"], check=True, capture_output=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        python_cmd = "python"

    print(f"Building openUSD with python_support={with_python_support}...")
    cmd = [
        python_cmd,
        f"{source_path}/build_scripts/build_usd.py",
        f"{install_path}",
        "--verbose",
        "--build-variant",
        "release", # always build openUSD as release to get it small. Unlikely to debug into it anyway
        "--build-monolithic",
        "--no-python" if not with_python_support else "--python",
        "--no-examples",
        "--no-tutorials",
        "--no-tools" if not with_python_support else "--tools",
        "--no-debug-python",
        "--no-openvdb",
        "--no-usdview",
        "--no-imaging",
        "--no-vulkan",
        "--no-materialx",
        "--onetbb",
        "--cmake-build-args",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_STANDARD=17",
    ]

    result = subprocess.run(cmd, env=build_env)
    if result.returncode != 0:
        Exit(f"openUSD build aborted (exit code: {result.returncode})")
        