"""
SCons tool: openusd
Builds the OpenUSD library from source using the provided build scripts.
The OpenUSD version can be configured via the 'openusd_version' variable in the SCons environment.
Usually the OpenUSD library is build without Python support, as the python bdinings are not required.
However, it can be enabled by passing 'with_python_support=True' to the BuildOpenUSD method.

Usage in SConstruct:
    env.BuildOpenUSD(with_python_support=False)  # Set to True to include Python bindings
"""
import os
import platform
import subprocess
import shutil

from SCons.Script import Exit

from cross_cmake import cmake_cross_args
from download_utils import download_file, extract_archive


def generate(env):
    env.AddMethod(_build_open_usd, 'BuildOpenUSD')

def exists(env):
    return True

def _build_open_usd(env, with_python_support=False):
    print("USD ROOT: " + env.get("OPENUSD_PATH", "<UNKNONW>"))
    open_usd_version = env["OPENUSD_VERSION"]
    open_usd_path =  env["OPENUSD_PATH"]
    open_usd_src_path = env["OPENUSD_SRC_PATH"]
    open_usd_build_script = f"{open_usd_src_path}/build_scripts/build_usd.py"
        
    # check if we have cloned openUSD already
    if not os.path.exists(open_usd_build_script):  # Check for a known file
        # Remove empty or incomplete directory before cloning
        if os.path.exists(open_usd_src_path):
            print("Cleaning up incomplete openUSD src folder")
            shutil.rmtree(open_usd_src_path)
    
        print("Cloning openUSD...")
        result = subprocess.run([
            "git", "clone", "-b", "v" + open_usd_version, "--recursive", "--depth", "2",
            "https://github.com/PixarAnimationStudios/OpenUSD.git",
            open_usd_src_path
        ])
        if result.returncode != 0:
            print(f"Failed to clone openUSD repo.")
            Exit(f"Build aborted due to subprocess failure (exit code: {result.returncode})")              

    # the python-enabled build only runs usdGenSchema on the build machine, so it is always a host build
    is_android = env.get('is_android', False) and not with_python_support
    platform_name = env["platform_name"] if not with_python_support else _host_platform_name()

    # check if we have build the openUSD lib already
    open_usd_build_path = open_usd_path if not with_python_support else f"{env['OPENUSD_HOST_PATH']}-withPython"
    if platform_name == "windows":
        open_usd_lib = f"{open_usd_build_path}/lib/usd_ms.dll"
    elif platform_name == "macos":
        open_usd_lib = f"{open_usd_build_path}/lib/libusd_ms.dylib"
    else:
        open_usd_lib = f"{open_usd_build_path}/lib/libusd_ms.so"
    
    if not os.path.exists(open_usd_lib) and is_android:
        _build_open_usd_android(env, open_usd_src_path, open_usd_build_path)
    elif not os.path.exists(open_usd_lib):
        print("Building openUSD...")
        if os.path.exists(open_usd_build_path):
            print("Cleaning up incomplete openUSD build folder")
            shutil.rmtree(open_usd_build_path)
            
        openusd_env = {}
        # when building openUSD we need to ensure that proper env-vars are set
        # on Windows
        if platform_name == "windows":
            _get_windows_msvc_env(openusd_env)
        else:
            # ensure the current system path is passed to the openUSD python build process
            openusd_env["PATH"] = os.environ.get("PATH", "")

        # Try python3 first, fallback to python if not available
        python_cmd = "python3"
        try:
            subprocess.run([python_cmd, "--version"], check=True, capture_output=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            python_cmd = "python"

        print(f"Building openUSD without python support = {with_python_support}...")
        result = subprocess.run([
            python_cmd,
            open_usd_build_script,
            open_usd_build_path,
            "--verbose",
            "--build-variant", "release", #debug,release,relwithdebuginfo
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
            "--cmake-build-args", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_STANDARD=17",
        ], env=openusd_env)
        
        if result.returncode != 0:
            print(f"Failed to build openUSD")
            Exit(f"Build aborted due to subprocess failure (exit code: {result.returncode})")        

def _host_platform_name():
    if platform.system() == "Windows":
        return "windows"
    if platform.system() == "Darwin":
        return "macos"
    return "linux"


# Same oneTBB version OpenUSD's own build_usd.py pins.
ONETBB_VERSION = "2021.12.0"
ONETBB_URL = f"https://github.com/oneapi-src/oneTBB/archive/refs/tags/v{ONETBB_VERSION}.zip"


def _run(cmd, what, env=None):
    result = subprocess.run(cmd, cwd=os.getcwd(), env=env)
    if result.returncode != 0:
        Exit(f"{what} failed (exit code: {result.returncode})")


def _cmake_build_install(build_dir, what):
    _run(["cmake", "--build", build_dir, "--config", "Release", "--parallel"], f"{what} build")
    # --strip: the NDK toolchain adds -g even to Release builds (libusd_ms.so would be ~570 MB)
    _run(["cmake", "--install", build_dir, "--config", "Release", "--strip"], f"{what} install")


def _build_tbb_android(env):
    """Download and cross-compile oneTBB for Android (build_usd.py cannot target Android). Returns the install prefix."""
    tbb_install_prefix = os.path.abspath(env["ONETBB_ANDROID_PATH"])
    if os.path.isfile(os.path.join(tbb_install_prefix, "include", "oneapi", "tbb.h")):
        print(f"oneTBB for Android already built: {tbb_install_prefix}")
        return tbb_install_prefix

    tbb_src_dir = os.path.abspath(f"thirdparty/onetbb-{ONETBB_VERSION}-src")
    if not os.path.exists(tbb_src_dir):
        zip_path = os.path.abspath(f"thirdparty/onetbb-{ONETBB_VERSION}.zip")
        download_file(ONETBB_URL, zip_path, "oneTBB")
        extract_archive(zip_path, "thirdparty")
        os.rename(os.path.abspath(f"thirdparty/oneTBB-{ONETBB_VERSION}"), tbb_src_dir)
        os.remove(zip_path)

    tbb_build_dir = os.path.abspath(f"thirdparty/onetbb-{ONETBB_VERSION}-android-build")
    print("Building oneTBB for Android...")
    _run([
        "cmake", f"-S{tbb_src_dir}", f"-B{tbb_build_dir}",
        *cmake_cross_args(env),
        f"-DCMAKE_INSTALL_PREFIX={tbb_install_prefix}",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DTBB_TEST=OFF",
        "-DTBB_STRICT=OFF",
        "-DBUILD_SHARED_LIBS=ON",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        # NDK 29+ ld.lld defaults to --no-undefined-version, which rejects version-script symbols that are not
        # present (e.g. the Intel compiler symbols in lin64-tbbmalloc.def)
        "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,--undefined-version",
    ], "oneTBB CMake configure for Android")
    _cmake_build_install(tbb_build_dir, "oneTBB for Android")
    return tbb_install_prefix


# ---------------------------------------------------------------------------
# Android source patches  (applied temporarily, reverted after build)
# ---------------------------------------------------------------------------

# Files that need patching relative to the OpenUSD source root.
_ANDROID_PATCH_FILES = [
    os.path.join("pxr", "base", "arch", "align.cpp"),
    os.path.join("pxr", "base", "arch", "daemon.cpp"),
    os.path.join("pxr", "base", "arch", "stackTrace.cpp"),
]

def _patch_openusd_for_android(open_usd_path):
    """Apply source patches and back up originals so they can be reverted later.

    Returns a list of (original, backup) path tuples for use by the revert
    function.  The caller **must** call ``_revert_openusd_android_patches``
    in a finally-block to restore the originals.
    """
    print("Applying temporary Android patches to OpenUSD source...")
    backup_pairs = []

    for rel in _ANDROID_PATCH_FILES:
        src = os.path.join(open_usd_path, rel)
        bak = src + ".android_bak"
        shutil.copy2(src, bak)
        backup_pairs.append((src, bak))

    # --- 1) align.cpp ---------------------------------------------------
    _patch_file(os.path.join(open_usd_path, "pxr", "base", "arch", "align.cpp"), [
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

    # --- 2) daemon.cpp ---------------------------------------------------
    _patch_file(os.path.join(open_usd_path, "pxr", "base", "arch", "daemon.cpp"), [
        (
            '#include <sys/param.h>',
            '#include <sys/param.h>\n'
            '#if defined(__ANDROID__) && !defined(NOFILE)\n'
            '#define NOFILE 256\n'
            '#endif',
        ),
    ])

    # --- 3) stackTrace.cpp -----------------------------------------------
    _patch_file(os.path.join(open_usd_path, "pxr", "base", "arch", "stackTrace.cpp"), [
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

    print("Android patches applied (backups saved with .android_bak suffix).")
    return backup_pairs


def _revert_openusd_android_patches(backup_pairs):
    """Restore original files from backups created by _patch_openusd_for_android."""
    print("Reverting Android patches from OpenUSD source...")
    for src, bak in backup_pairs:
        if os.path.exists(bak):
            shutil.move(bak, src)
    print("OpenUSD source restored to original state.")


def _patch_file(filepath, replacements):
    """Apply a list of (old, new) string replacements to a file."""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    for old, new in replacements:
        if old not in content:
            Exit(f"  ERROR: Android patch pattern not found in {filepath}:\n"
                 f"    {old[:120]}\n"
                 f"  The OpenUSD source may have changed. Review the Android patches in openusd.py.")
        content = content.replace(old, new, 1)

    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(content)


def _build_open_usd_android(env, open_usd_path, open_usd_build_path):
    """Cross-compile OpenUSD for Android with plain CMake + the NDK toolchain (build_usd.py has no Android target).

    Temporarily patches the OpenUSD source for Android compatibility and **always** reverts the patches afterwards
    (even on build failure).
    """
    print("Building openUSD for Android (cross-compilation via NDK)...")
    tbb_root = _build_tbb_android(env)
    cmake_build_dir = os.path.abspath(f"{open_usd_build_path}-build")

    backup_pairs = _patch_openusd_for_android(open_usd_path)
    try:
        _run([
            "cmake", f"-S{os.path.abspath(open_usd_path)}", f"-B{cmake_build_dir}",
            *cmake_cross_args(env),
            f"-DCMAKE_INSTALL_PREFIX={os.path.abspath(open_usd_build_path)}",
            # Release only: debug info inflated libusd_ms.so to ~590 MB
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_CXX_STANDARD=17",
            "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
            "-DBUILD_SHARED_LIBS=ON",
            "-DPXR_BUILD_MONOLITHIC=ON",
            # Disable features not needed/supported on Android
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
            # TBB: the cross-compiled oneTBB installation
            f"-DTBB_DIR={os.path.join(tbb_root, 'lib', 'cmake', 'TBB')}",
            f"-DCMAKE_PREFIX_PATH={tbb_root}",
            "-DPXR_FIND_TBB_IN_CONFIG=ON",
        ], "OpenUSD CMake configure for Android")
        _cmake_build_install(cmake_build_dir, "OpenUSD for Android")
    finally:
        _revert_openusd_android_patches(backup_pairs)
    print(f"OpenUSD for Android built successfully: {open_usd_build_path}")


def _get_windows_msvc_env(env):
    vswhere_path = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if not os.path.exists(vswhere_path):
        raise RuntimeError("vswhere.exe not found")

    # Step 1: Find the installation path of Visual Studio
    cmd = [
        vswhere_path,
        "-latest",
        "-products", "*",
        "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "-property", "installationPath"
    ]
    vs_path = subprocess.check_output(cmd, encoding="utf-8").strip()
    if not vs_path:
        raise RuntimeError("No Visual Studio installation with required components found")
    
    """Runs vcvars64.bat and returns its environment as a dict"""
    vcvars_path = os.path.join(vs_path, "VC", "Auxiliary", "Build", "vcvars64.bat")
    
    # Use a cmd trick to output all environment variables after calling vcvars
    cmd = f'"{vcvars_path}" >nul && set'
    
    # Run and capture output
    output = subprocess.check_output(cmd, shell=True, text=True)
    
    # Parse into a dictionary
    for line in output.splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            env[key.upper()] = value
            
    return env
