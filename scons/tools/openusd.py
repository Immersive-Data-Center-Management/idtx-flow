"""SCons tool: openusd (refactored)

Two backends:
  - Desktop (Windows/Linux/macOS): delegate to OpenUSD's upstream
    ``build_usd.py`` Python script.
  - Android: cross-compile via CMake using the NDK toolchain. Includes a
    one-shot oneTBB cross-build (Android-only, OpenUSD requires it) and a
    set of Android-specific source patches that are applied/reverted
    around the build.

Both backends are kept in this single file because they are *only* used
by this tool. Splitting them out would just add files without reducing
duplication.

Registers the SCons env method ``BuildOpenUSDV2``.
"""
import os
import platform
import shutil
import subprocess
import sys

from SCons.Script import Exit

from scons.core import cmake_runner, msvc_env
from scons.core.run import run_or_exit
from scons.platforms import android as android_platform


# ---------------------------------------------------------------------------
# oneTBB version (Android-only — OpenUSD bundles its own TBB on desktop)
# ---------------------------------------------------------------------------

ONETBB_VERSION = "2021.12.0"
ONETBB_URL     = f"https://github.com/oneapi-src/oneTBB/archive/refs/tags/v{ONETBB_VERSION}.zip"


def generate(env):
    env.AddMethod(_build_openusd, "BuildOpenUSDV2")


def exists(env):
    return True


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def _build_openusd(env, with_python_support: bool = False):
    profile  = env["profile"]
    platform = profile.platform
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
    if platform.is_android:
        install_path = paths.openusd_install
    elif with_python_support:
        install_path = paths.openusd_python
    else:
        install_path = paths.openusd_install

    # Already-built probe — short-circuits subsequent invocations
    if platform.is_windows:
        marker = f"{install_path}/lib/usd_ms.dll"
    elif platform.is_macos:
        marker = f"{install_path}/lib/libusd_ms.dylib"
    else:
        marker = f"{install_path}/lib/libusd_ms.so"

    if os.path.exists(marker):
        return

    if platform.is_android:
        _build_for_android(env, paths.openusd_src, install_path, platform.target)
    else:
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
        "release" if platform.target == "template_release" else "relwithdebuginfo",
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


# ---------------------------------------------------------------------------
# Android backend (CMake direct, with source patches)
# ---------------------------------------------------------------------------

def _build_for_android(env, source_path, install_path, build_target):
    """Cross-compile OpenUSD for Android. Patches are applied/reverted around
    the build so the on-disk source tree is left untouched."""
    print("Building openUSD for Android (cross-compilation via NDK)...")

    # Patch source — must always be reverted, even on build failure
    backups = _patch_openusd_for_android(source_path)
    try:
        _do_build_android(env, source_path, install_path, build_target)
    finally:
        _revert_android_patches(backups)


def _do_build_android(env, source_path, install_path, build_target):
    # Build oneTBB for Android first (OpenUSD requires it)
    tbb_root = _build_tbb_android(env)

    ndk_root  = env.get("android_ndk_root", os.environ.get("ANDROID_NDK_ROOT", ""))
    api_level = env.get("android_api_level", 24)

    cmake_build_dir = f"{install_path}-build"
    os.makedirs(install_path, exist_ok=True)

    build_variant = "Release" if build_target == "template_release" else "RelWithDebInfo"

    cmake_args = [
        *android_platform.cmake_toolchain_args(ndk_root, api_level),
        f"-DCMAKE_INSTALL_PREFIX={os.path.abspath(install_path)}",
        f"-DCMAKE_BUILD_TYPE={build_variant}",
        "-DCMAKE_CXX_STANDARD=17",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        "-DBUILD_SHARED_LIBS=ON",
        "-DPXR_BUILD_MONOLITHIC=ON",
        "-DPXR_BUILD_IMAGING=OFF",
        "-DPXR_BUILD_USD_IMAGING=OFF",
        "-DPXR_BUILD_USDVIEW=OFF",
        "-DPXR_BUILD_TESTS=OFF",
        "-DPXR_BUILD_EXAMPLES=OFF",
        "-DPXR_BUILD_TUTORIALS=OFF",
        "-DPXR_BUILD_DOCUMENTATION=OFF",
        "-DPXR_ENABLE_PYTHON_SUPPORT=OFF",
        "-DPXR_ENABLE_MATERIALX_SUPPORT=OFF",
        "-DPXR_ENABLE_OPENVDB_SUPPORT=OFF",
        "-DPXR_ENABLE_VULKAN_SUPPORT=OFF",
        f"-DTBB_ROOT={tbb_root}",
        f"-DTBB_DIR={os.path.join(tbb_root, 'lib', 'cmake', 'TBB')}",
        f"-DCMAKE_PREFIX_PATH={tbb_root}",
        "-DPXR_FIND_TBB_IN_CONFIG=ON",
    ]

    cmake_runner.configure(source_path, cmake_build_dir, cmake_args, "OpenUSD-Android")
    cmake_runner.build(cmake_build_dir, "OpenUSD-Android", config=build_variant)
    cmake_runner.install(cmake_build_dir, "OpenUSD-Android", config=build_variant)

    print(f"OpenUSD for Android built successfully: {install_path}")


# ---------------------------------------------------------------------------
# oneTBB Android cross-build  (only used by this tool)
# ---------------------------------------------------------------------------

def _build_tbb_android(env):
    """Download + cross-compile oneTBB for Android. Returns the install prefix."""
    tbb_install_prefix = os.path.abspath("thirdparty/onetbb-android")
    tbb_marker = os.path.join(tbb_install_prefix, "include", "oneapi", "tbb.h")

    if os.path.isfile(tbb_marker):
        print(f"oneTBB for Android already built: {tbb_install_prefix}")
        return tbb_install_prefix

    tbb_src_dir = os.path.abspath(f"thirdparty/onetbb-{ONETBB_VERSION}-src")
    if not os.path.exists(tbb_src_dir):
        import urllib.request
        import zipfile

        print(f"Downloading oneTBB v{ONETBB_VERSION}...")
        zip_path = os.path.abspath(f"thirdparty/onetbb-{ONETBB_VERSION}.zip")
        urllib.request.urlretrieve(ONETBB_URL, zip_path)

        print("Extracting oneTBB...")
        with zipfile.ZipFile(zip_path, "r") as zf:
            zf.extractall("thirdparty")

        extracted_dir = os.path.abspath(f"thirdparty/oneTBB-{ONETBB_VERSION}")
        if os.path.isdir(extracted_dir) and not os.path.isdir(tbb_src_dir):
            os.rename(extracted_dir, tbb_src_dir)

        if os.path.exists(zip_path):
            os.remove(zip_path)

    ndk_root  = env.get("android_ndk_root", os.environ.get("ANDROID_NDK_ROOT", ""))
    api_level = env.get("android_api_level", 24)

    tbb_build_dir = os.path.abspath(f"thirdparty/onetbb-{ONETBB_VERSION}-android-build")
    os.makedirs(tbb_install_prefix, exist_ok=True)

    cmake_args = [
        *android_platform.cmake_toolchain_args(ndk_root, api_level),
        f"-DCMAKE_INSTALL_PREFIX={tbb_install_prefix}",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DTBB_TEST=OFF",
        "-DTBB_STRICT=OFF",
        "-DBUILD_SHARED_LIBS=ON",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        # NDK 29+ ld.lld errors on missing version-script symbols; restore
        # the old permissive behaviour.
        "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,--undefined-version",
    ]

    cmake_runner.configure(tbb_src_dir, tbb_build_dir, cmake_args, "oneTBB-Android")
    cmake_runner.build(tbb_build_dir, "oneTBB-Android")
    cmake_runner.install(tbb_build_dir, "oneTBB-Android")

    print(f"oneTBB for Android built successfully: {tbb_install_prefix}")
    return tbb_install_prefix


# ---------------------------------------------------------------------------
# Android source patches  (applied temporarily, reverted after build)
# ---------------------------------------------------------------------------

# Files patched relative to the OpenUSD source root
_ANDROID_PATCH_FILES = [
    os.path.join("pxr", "base", "arch", "align.cpp"),
    os.path.join("pxr", "base", "arch", "daemon.cpp"),
    os.path.join("pxr", "base", "arch", "stackTrace.cpp"),
    os.path.join("pxr", "usd", "usdGeom", "bboxCache.h"),
    os.path.join("pxr", "usd", "usdGeom", "bboxCache.cpp"),
]


def _patch_openusd_for_android(source_path):
    """Apply Android source patches; return [(path, backup_path), ...] for revert."""
    print("Applying temporary Android patches to OpenUSD source...")
    backups = []
    for rel in _ANDROID_PATCH_FILES:
        src = os.path.join(source_path, rel)
        bak = src + ".android_bak"
        shutil.copy2(src, bak)
        backups.append((src, bak))

    # 1) align.cpp — treat __ANDROID__ like Darwin / glibcxx-without-aligned_alloc
    _patch_file(os.path.join(source_path, "pxr", "base", "arch", "align.cpp"), [
        (
            '#if defined(ARCH_OS_DARWIN) || (defined(ARCH_OS_LINUX) && defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC))\n'
            '    // alignment must be >= sizeof(void*)',
            '#if defined(ARCH_OS_DARWIN) || defined(__ANDROID__) || (defined(ARCH_OS_LINUX) && defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC))\n'
            '    // alignment must be >= sizeof(void*)',
        ),
        (
            '#if defined(ARCH_OS_DARWIN) || (defined(ARCH_OS_LINUX) && defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC))\n'
            '    free(ptr);',
            '#if defined(ARCH_OS_DARWIN) || defined(__ANDROID__) || (defined(ARCH_OS_LINUX) && defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC))\n'
            '    free(ptr);',
        ),
    ])

    # 2) daemon.cpp — define NOFILE on Android
    _patch_file(os.path.join(source_path, "pxr", "base", "arch", "daemon.cpp"), [
        (
            '#include <sys/param.h>',
            '#include <sys/param.h>\n'
            '#if defined(__ANDROID__) && !defined(NOFILE)\n'
            '#define NOFILE 256\n'
            '#endif',
        ),
    ])

    # 3) stackTrace.cpp — environ vs __environ on Android
    _patch_file(os.path.join(source_path, "pxr", "base", "arch", "stackTrace.cpp"), [
        (
            '#if defined(ARCH_OS_LINUX)\n'
            '     return nonLockingLinux__execve (path, argv, __environ);',
            '#if defined(ARCH_OS_LINUX)\n'
            '#if defined(__ANDROID__)\n'
            '     return nonLockingLinux__execve (path, argv, environ);\n'
            '#else\n'
            '     return nonLockingLinux__execve (path, argv, __environ);\n'
            '#endif',
        ),
    ])

    # 4) bboxCache.h — switch shared_ptr<T[]> to shared_ptr<T> for NDK 25 libc++
    _patch_file(os.path.join(source_path, "pxr", "usd", "usdGeom", "bboxCache.h"), [
        (
            'std::shared_ptr<UsdAttributeQuery[]> queries;',
            'std::shared_ptr<UsdAttributeQuery> queries;',
        ),
    ])

    # 5) bboxCache.cpp — adapt usages to non-array shared_ptr
    _patch_file(os.path.join(source_path, "pxr", "usd", "usdGeom", "bboxCache.cpp"), [
        (
            'std::shared_ptr<UsdAttributeQuery[]> &queries = entry->queries;',
            'std::shared_ptr<UsdAttributeQuery> &queries = entry->queries;',
        ),
        (
            'queries.reset(new UsdAttributeQuery[numQueries]);',
            'queries.reset(new UsdAttributeQuery[numQueries], std::default_delete<UsdAttributeQuery[]>());',
        ),
        (
            '&queries[ExtentsHint]',
            '&queries.get()[ExtentsHint]',
        ),
        (
            '&queries[Extent]',
            '&queries.get()[Extent]',
        ),
    ])

    print("Android patches applied (backups saved with .android_bak suffix).")
    return backups


def _revert_android_patches(backups):
    print("Reverting Android patches from OpenUSD source...")
    for src, bak in backups:
        if os.path.exists(bak):
            shutil.move(bak, src)
    print("OpenUSD source restored to original state.")


def _patch_file(filepath, replacements):
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()

    for old, new in replacements:
        if old not in content:
            print(f"  WARNING: patch pattern not found in {filepath}:")
            print(f"    {old[:80]}...")
            continue
        content = content.replace(old, new, 1)

    with open(filepath, "w", encoding="utf-8") as f:
        f.write(content)
