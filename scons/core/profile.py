"""BuildProfile — the declarative result of platform/target/arch decisions.

This is the *single* place where "what goes on the env for this build"
is decided. Tools never compute flags themselves; they read pre-computed
sections from the profile and hand them to the mechanical applicator in
``core/apply.py``.

Three layers, each obvious in its role:

  - **Profile (this file)** — pure data. Inspecting a ``BuildProfile``
    tells you everything the build will append to env.
  - **Applicator (``core/apply.py``)** — mechanical. Spreads a profile
    section into ``env.Append`` calls. No decisions.
  - **Side-effecting helpers (``core/cmake_runner.py``,
    ``core/openssl_locator.py``, ``core/vcpkg.py``, ``core/msvc_env.py``,
    ``core/run.py``, the ``ThirdPartySource.ensure()`` machinery)** —
    procedural. Run subprocesses, probe the filesystem, download files.
    The profile *feeds* them with declarative inputs (e.g.
    ``profile.cmake_toolchain_args``).

The profile is constructed once at SConstruct startup (via
``build_profile()``) and stashed on ``env["profile"]``.
"""
from dataclasses import dataclass, field
from typing import Mapping, Tuple, Callable

from .platform_info import PlatformInfo
from .paths import BuildPaths
from . import lib_naming
from scons.platforms import android as android_platform


# ---------------------------------------------------------------------------
# BuildProfile
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class BuildProfile:
    """Frozen, fully-resolved description of how to build for this target."""

    platform: PlatformInfo
    paths: BuildPaths

    # ---- Sections to feed env.Append (just dicts of {key: [values...]}) ----
    cxx_flags: Mapping[str, list]
    """Universal C++ compile/link flags. Apply once on the root env, and
    again on each Cloned env that compiles C++ (since SCons tools do
    Clone() and therefore inherit only what was on the root *at clone
    time*)."""

    shared_lib_flags: Mapping[str, list]
    """Extra LIBS / LINKFLAGS for shared-library targets (rpath,
    install_name, system libs like dl/pthread/log/m). Apply only on envs
    that build shared libraries — not on static libs."""

    msvc_extras: Mapping[str, list]
    """MSVC-only extras (NOMINMAX, WIN32_LEAN_AND_MEAN, ...). Empty
    elsewhere; safe to always apply."""

    gdextension_extras: Mapping[str, list]
    """Extras specific to the main GDExtension shared lib build (e.g.
    /arch:AVX2). Empty when not applicable."""

    # ---- Cross-compile inputs (consumed by cmake_runner / sub-builds) -----
    cmake_toolchain_args: Tuple[str, ...]
    """Args fed to ``cmake -S ... -B ...`` for cross-compile builds.
    Empty tuple on desktop. Used by ixwebsocket and openusd-android."""

    scons_env_overrides: Mapping[str, str]
    """CC/CXX/AR/... overrides to pass into ``Environment(**...)`` when
    SCons itself drives the compiler. Empty on desktop. Used only at the
    top of SConstruct."""

    # ---- Library naming (a few constants + factory functions) -------------
    tbb_link_name: str

    # The two callables are intentionally Python-callable rather than
    # @methods so they survive the dataclass(frozen=True) guarantee.
    shared_lib_filename: Callable[[str], str] = field(repr=False)
    static_lib_filename: Callable[[str], str] = field(repr=False)


# ---------------------------------------------------------------------------
# Factory
# ---------------------------------------------------------------------------

OPENUSD_VERSION_DEFAULT = "26.05"


def build_profile(args, openusd_version: str = OPENUSD_VERSION_DEFAULT) -> BuildProfile:
    """Compute the full BuildProfile from SCons ARGUMENTS.

    Side effects: validates ANDROID_NDK_ROOT when targeting Android (calls
    Exit on failure). Otherwise pure.
    """
    platform = PlatformInfo.detect(args)
    paths    = BuildPaths(platform=platform, openusd_version=openusd_version)

    cxx, shared, msvc, gdext = {}, {}, {}, {}
    cmake_args: Tuple[str, ...] = ()
    env_overrides: Mapping[str, str] = {}

    if platform.is_msvc:
        # ---- Windows / MSVC ------------------------------------------------
        cxx = {
            "CXXFLAGS": ["/EHsc", "/GR", "/FS", "/std:c++20"],
            "CCFLAGS":  (["/O2", "/MT", '/wd4273'] if platform.target == "template_release"
                         else ["/Zi", "/Od", "/MT", "/FS", "/wd4273", "/Fd${TARGET}.pdb"]),
            "LINKFLAGS": ([] if platform.target == "template_release"
                          else ["/DEBUG"]),
        }
        shared = {
            "LIBS": ["advapi32", "shell32", "ole32",
                     "ws2_32", "crypt32", "user32"],
        }
        msvc = {
            "CPPDEFINES": ["NOMINMAX", "WIN32_LEAN_AND_MEAN",
                           "_ITERATOR_DEBUG_LEVEL=0"],
        }
        gdext = {"CXXFLAGS": ["/arch:AVX2"]}
    else:
        # ---- GCC / Clang / NDK Clang baseline ------------------------------
        cxx = {
            "CXXFLAGS": ["-fexceptions", "-frtti", "-std=c++20"],
            "CCFLAGS":  ["-fPIC", "-O3" if platform.target == "template_release" else "-g"],
        }

        if platform.is_linux:
            shared = {
                "LIBS":      ["dl", "pthread", "m"],
                "LINKFLAGS": ["-Wl,-rpath,$ORIGIN"],
                "CCFLAGS":   ["-fPIC", "-g", "-frtti"],
            }
        elif platform.is_macos:
            shared = {
                "CCFLAGS":   ["-fPIC", "-g", "-Og", "-O0", "-frtti"],
                "LINKFLAGS": ["-framework", "CoreFoundation",
                              "-Wl,-rpath,@loader_path", "-g"],
            }
            # macOS install_name needs the per-library filename, which
            # callers know better than the profile does. Tools append it
            # individually using profile.shared_lib_filename(base).
        elif platform.is_android:
            ndk_root  = android_platform.resolve_ndk_root(args)
            api_level = int(args.get("android_api_level", "30"))

            triple = f"aarch64-linux-android{api_level}"
            cxx["CCFLAGS"]   = list(cxx["CCFLAGS"]) + [f"--target={triple}", "-march=armv8-a"]
            cxx["LINKFLAGS"] = [f"--target={triple}", "-march=armv8-a"]

            shared = {
                "LIBS":      ["log", "android", "dl", "m"],
                "LINKFLAGS": ["-Wl,-z,relro", "-Wl,-z,now"],
                "CCFLAGS":   ["-fPIC", "-frtti"],
            }
            cmake_args    = tuple(android_platform.cmake_toolchain_args(ndk_root, api_level))
            env_overrides = android_platform.scons_env_overrides(ndk_root)

    return BuildProfile(
        platform=platform,
        paths=paths,
        cxx_flags=cxx,
        shared_lib_flags=shared,
        msvc_extras=msvc,
        gdextension_extras=gdext,
        cmake_toolchain_args=cmake_args,
        scons_env_overrides=env_overrides,
        tbb_link_name=lib_naming.tbb_link_name(platform),
        shared_lib_filename=(lambda base, _p=platform: lib_naming.shared_lib_filename(_p, base)),
        static_lib_filename=(lambda base, _p=platform: lib_naming.static_lib_filename(_p, base)),
    )