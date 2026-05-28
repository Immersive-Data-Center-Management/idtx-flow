"""
SCons tool: openusd
Builds the OpenUSD library from source using the provided build scripts.
The built OpenUSD library is a dependency for the IDTXFlow GDExtension. The OpenUSD version
can be configured via the 'openusd_version' variable in the SCons environment. Usually the OpenUSD library is
build without Python support, as the IDTXFlow GDExtension does not require it. However, you can enable Python support
by passing 'with_python_support=True' to the BuildOpenUSD method.

Usage in SConstruct:
    env.BuildOpenUSD(with_python_support=False)  # Set to True to include Python bindings
"""
import os
import platform
import subprocess

from SCons.Script import Exit


def generate(env):
    env.AddMethod(_build_open_usd, 'BuildOpenUSD')

def exists(env):
    return True

def _build_open_usd(env, with_python_support=False):
    open_usd_version = env.get('openusd_version', '')
    open_usd_path = f"thirdparty/openusd-{open_usd_version}-src"
    print("USD ROOT" + os.environ.get("USD_ROOT", "thirdparty/openusd"))
    
    # check if we have cloned openUSD already
    if not os.path.exists(open_usd_path):
        print("Cloning openUSD...")
        result = subprocess.run([
            "git", "clone", "-b", "v" + open_usd_version, "--recursive", "--depth", "2",
            "https://github.com/PixarAnimationStudios/OpenUSD.git",
            open_usd_path
        ])
        if result.returncode != 0:
            print(f"Failed to clone openUSD repo.")
            Exit(f"Build aborted due to subprocess failure (exit code: {result.returncode})")              

    platform_name = env["platform_name"]
    build_target = env["target"]
    is_android = env.get('is_android', False)
    is_wasm = env.get('is_wasm', False)
    wasm_target = env.get('wasm_target', None)

    # check if we have build the openUSD lib already
    if is_android:
        open_usd_build_path = f"thirdparty/openusd-{open_usd_version}-android"
    elif is_wasm:
        open_usd_build_path = f"thirdparty/openusd-{open_usd_version}-{wasm_target}"
    elif with_python_support:
        open_usd_build_path = f"thirdparty/openusd-{open_usd_version}-withPython"
    else:
        open_usd_build_path = f"thirdparty/openusd-{open_usd_version}"

    if platform_name == "windows":
        open_usd_lib = f"{open_usd_build_path}/lib/usd_ms.dll"
    elif platform_name == "macos":
        open_usd_lib = f"{open_usd_build_path}/lib/libusd_ms.dylib"
    elif is_wasm:
        # WebAssembly monolithic static library is named libusd_m.a
        open_usd_lib = f"{open_usd_build_path}/lib/libusd_m.a"
    else:
        # Linux and Android both produce .so
        open_usd_lib = f"{open_usd_build_path}/lib/libusd_ms.so"
    
    if not os.path.exists(open_usd_lib):
        if is_android:
            _build_open_usd_android(env, open_usd_path, open_usd_build_path, build_target)
        elif is_wasm:
            _build_open_usd_wasm(env, open_usd_path, open_usd_build_path, wasm_target, build_target)
        else:
            _build_open_usd_desktop(env, open_usd_path, open_usd_build_path,
                                     platform_name, build_target, with_python_support)


def _get_android_ninja_args(env):
    """Build the -G Ninja + CMAKE_MAKE_PROGRAM args for Android cross-compilation."""
    ninja_args = ["-G", "Ninja"]
    sdk_root = env.get('android_sdk_root',
                       os.environ.get('ANDROID_HOME',
                       os.environ.get('ANDROID_SDK_ROOT', '')))
    if sdk_root:
        ninja_dir = os.path.join(sdk_root, "cmake")
        if os.path.isdir(ninja_dir):
            for entry in sorted(os.listdir(ninja_dir), reverse=True):
                ninja_candidate = os.path.join(ninja_dir, entry, "bin",
                                               "ninja.exe" if platform.system() == "Windows" else "ninja")
                if os.path.isfile(ninja_candidate):
                    ninja_args.append(f"-DCMAKE_MAKE_PROGRAM={ninja_candidate}")
                    break
    return ninja_args


ONETBB_VERSION = "2021.12.0"
ONETBB_URL = f"https://github.com/oneapi-src/oneTBB/archive/refs/tags/v{ONETBB_VERSION}.zip"


def _build_tbb_android(env):
    """Download and cross-compile oneTBB for Android. Returns the install prefix."""
    tbb_install_prefix = os.path.abspath("thirdparty/onetbb-android")
    tbb_marker = os.path.join(tbb_install_prefix, "include", "oneapi", "tbb.h")

    if os.path.isfile(tbb_marker):
        print(f"oneTBB for Android already built: {tbb_install_prefix}")
        return tbb_install_prefix

    # Download oneTBB source
    tbb_src_dir = os.path.abspath(f"thirdparty/onetbb-{ONETBB_VERSION}-src")
    if not os.path.exists(tbb_src_dir):
        import zipfile
        import urllib.request
        import io

        print(f"Downloading oneTBB v{ONETBB_VERSION}...")
        zip_path = os.path.abspath(f"thirdparty/onetbb-{ONETBB_VERSION}.zip")
        urllib.request.urlretrieve(ONETBB_URL, zip_path)

        print("Extracting oneTBB...")
        with zipfile.ZipFile(zip_path, 'r') as zf:
            zf.extractall("thirdparty")

        # The archive extracts to oneTBB-<version>/
        extracted_dir = os.path.abspath(f"thirdparty/oneTBB-{ONETBB_VERSION}")
        if os.path.isdir(extracted_dir) and not os.path.isdir(tbb_src_dir):
            os.rename(extracted_dir, tbb_src_dir)

        # Clean up zip
        if os.path.exists(zip_path):
            os.remove(zip_path)

    ndk_root = env.get('android_ndk_root', os.environ.get('ANDROID_NDK_ROOT', ''))
    api_level = env.get('android_api_level', 24)
    ndk_toolchain_file = os.path.join(ndk_root, "build", "cmake", "android.toolchain.cmake")

    tbb_build_dir = os.path.abspath(f"thirdparty/onetbb-{ONETBB_VERSION}-android-build")
    os.makedirs(tbb_build_dir, exist_ok=True)
    os.makedirs(tbb_install_prefix, exist_ok=True)

    ninja_args = _get_android_ninja_args(env)

    print("Configuring oneTBB for Android...")
    cmake_configure = [
        "cmake",
        f"-S{tbb_src_dir}",
        f"-B{tbb_build_dir}",
        *ninja_args,
        f"-DCMAKE_TOOLCHAIN_FILE={ndk_toolchain_file}",
        "-DANDROID_ABI=arm64-v8a",
        f"-DANDROID_PLATFORM=android-{api_level}",
        "-DANDROID_STL=c++_shared",
        f"-DCMAKE_INSTALL_PREFIX={tbb_install_prefix}",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DTBB_TEST=OFF",
        "-DTBB_STRICT=OFF",
        "-DBUILD_SHARED_LIBS=ON",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        # NDK 29+ ld.lld enables --no-undefined-version by default, which
        # errors on version-script symbols that are not present (e.g. Intel
        # Compiler / libirc symbols in lin64-tbbmalloc.def).  Override with
        # --undefined-version to restore the old permissive behaviour.
        "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,--undefined-version",
    ]

    result = subprocess.run(cmake_configure, cwd=os.getcwd())
    if result.returncode != 0:
        Exit(f"oneTBB CMake configure for Android failed (exit code: {result.returncode})")

    print("Building oneTBB for Android...")
    result = subprocess.run([
        "cmake", "--build", tbb_build_dir, "--config", "Release", "--parallel",
    ], cwd=os.getcwd())
    if result.returncode != 0:
        Exit(f"oneTBB build for Android failed (exit code: {result.returncode})")

    print("Installing oneTBB for Android...")
    result = subprocess.run([
        "cmake", "--install", tbb_build_dir, "--config", "Release",
    ], cwd=os.getcwd())
    if result.returncode != 0:
        Exit(f"oneTBB install for Android failed (exit code: {result.returncode})")

    print(f"oneTBB for Android built successfully: {tbb_install_prefix}")
    return tbb_install_prefix


# ---------------------------------------------------------------------------
# Android source patches  (applied temporarily, reverted after build)
# ---------------------------------------------------------------------------

# Files that need patching relative to the OpenUSD source root.
_ANDROID_PATCH_FILES = [
    os.path.join("pxr", "base", "arch", "align.cpp"),
    os.path.join("pxr", "base", "arch", "daemon.cpp"),
    os.path.join("pxr", "base", "arch", "stackTrace.cpp"),
    os.path.join("pxr", "usd", "usdGeom", "bboxCache.h"),
    os.path.join("pxr", "usd", "usdGeom", "bboxCache.cpp"),
]

import shutil


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

    # --- 4) bboxCache.h -- shared_ptr<T[]> → shared_ptr<T> (non-array) --
    #     NDK 25's libc++ has a broken shared_ptr<T[]> specialisation
    #     (constructors, reset, operator[], get() all fail).
    #     We switch to shared_ptr<T> (non-array) with an explicit array
    #     deleter.  shared_ptr<T> is copyable (required by _Entry in
    #     TfHashMap) and fully functional in NDK 25.
    _patch_file(os.path.join(open_usd_path, "pxr", "usd", "usdGeom", "bboxCache.h"), [
        (
            'std::shared_ptr<UsdAttributeQuery[]> queries;',
            'std::shared_ptr<UsdAttributeQuery> queries;',
        ),
    ])

    # --- 5) bboxCache.cpp -- adapt usages to non-array shared_ptr --------
    _patch_file(os.path.join(open_usd_path, "pxr", "usd", "usdGeom", "bboxCache.cpp"), [
        # Change the local reference type
        (
            'std::shared_ptr<UsdAttributeQuery[]> &queries = entry->queries;',
            'std::shared_ptr<UsdAttributeQuery> &queries = entry->queries;',
        ),
        # .reset(new T[n]) → .reset(new T[n], default_delete<T[]>())
        (
            'queries.reset(new UsdAttributeQuery[numQueries]);',
            'queries.reset(new UsdAttributeQuery[numQueries], std::default_delete<UsdAttributeQuery[]>());',
        ),
        # queries[i] → queries.get()[i]  (shared_ptr<T> has no operator[])
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


def _build_open_usd_android(env, open_usd_path, open_usd_build_path, build_target):
    """Cross-compile OpenUSD for Android using CMake + NDK toolchain.

    Temporarily patches the OpenUSD source for Android compatibility and
    **always** reverts the patches afterwards (even on build failure).
    """
    print("Building openUSD for Android (cross-compilation via NDK)...")

    # Apply temporary Android patches (backed up so we can revert)
    backup_pairs = _patch_openusd_for_android(open_usd_path)

    try:
        _do_build_open_usd_android(env, open_usd_path, open_usd_build_path, build_target)
    finally:
        _revert_openusd_android_patches(backup_pairs)


def _do_build_open_usd_android(env, open_usd_path, open_usd_build_path, build_target):
    """Inner build logic for Android OpenUSD (called within patch try/finally)."""

    # Build oneTBB for Android first (OpenUSD requires it)
    tbb_root = _build_tbb_android(env)

    ndk_root = env.get('android_ndk_root', os.environ.get('ANDROID_NDK_ROOT', ''))
    api_level = env.get('android_api_level', 24)
    ndk_toolchain_file = os.path.join(ndk_root, "build", "cmake", "android.toolchain.cmake")

    cmake_build_dir = f"{open_usd_build_path}-build"
    os.makedirs(cmake_build_dir, exist_ok=True)
    os.makedirs(open_usd_build_path, exist_ok=True)

    build_variant = "Release" if build_target == "template_release" else "RelWithDebInfo"

    ninja_args = _get_android_ninja_args(env)

    cmake_configure = [
        "cmake",
        f"-S{os.path.abspath(open_usd_path)}",
        f"-B{os.path.abspath(cmake_build_dir)}",
        *ninja_args,
        f"-DCMAKE_TOOLCHAIN_FILE={ndk_toolchain_file}",
        "-DANDROID_ABI=arm64-v8a",
        f"-DANDROID_PLATFORM=android-{api_level}",
        "-DANDROID_STL=c++_shared",
        f"-DCMAKE_INSTALL_PREFIX={os.path.abspath(open_usd_build_path)}",
        f"-DCMAKE_BUILD_TYPE={build_variant}",
        "-DCMAKE_CXX_STANDARD=17",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        # Build monolithic shared library
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
        # TBB: point at the cross-compiled oneTBB installation
        f"-DTBB_ROOT={tbb_root}",
        f"-DTBB_DIR={os.path.join(tbb_root, 'lib', 'cmake', 'TBB')}",
        f"-DCMAKE_PREFIX_PATH={tbb_root}",
        "-DPXR_FIND_TBB_IN_CONFIG=ON",
    ]

    result = subprocess.run(cmake_configure, cwd=os.getcwd())
    if result.returncode != 0:
        print(f"OpenUSD CMake configure for Android failed (exit code: {result.returncode})")
        Exit(f"OpenUSD CMake configure for Android failed (exit code: {result.returncode})")

    cmake_build = [
        "cmake",
        "--build", os.path.abspath(cmake_build_dir),
        "--config", build_variant,
        "--parallel",
    ]

    result = subprocess.run(cmake_build, cwd=os.getcwd())
    if result.returncode != 0:
        print(f"OpenUSD build for Android failed (exit code: {result.returncode})")
        Exit(f"OpenUSD build for Android failed (exit code: {result.returncode})")

    cmake_install = [
        "cmake",
        "--install", os.path.abspath(cmake_build_dir),
        "--config", build_variant,
    ]

    result = subprocess.run(cmake_install, cwd=os.getcwd())
    if result.returncode != 0:
        print(f"OpenUSD install for Android failed (exit code: {result.returncode})")
        Exit(f"OpenUSD install for Android failed (exit code: {result.returncode})")

    print(f"OpenUSD for Android built successfully: {open_usd_build_path}")


def _build_open_usd_desktop(env, open_usd_path, open_usd_build_path,
                             platform_name, build_target, with_python_support):
    """Build OpenUSD for desktop platforms using the upstream build_usd.py script."""
    print("Building openUSD...")
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
        f"{open_usd_path}/build_scripts/build_usd.py",
        f"{open_usd_build_path}",
        "--verbose",
        "--build-variant", "release" if build_target == "template_release" else "relwithdebuginfo", #debug,release,relwithdebuginfo
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

def _build_open_usd_wasm(env, open_usd_path, open_usd_build_path, wasm_target, build_target):
    """Build OpenUSD for WebAssembly using the upstream build_usd.py script.
    
    Args:
        env: SCons environment with emsdk_root configured
        open_usd_path: Path to OpenUSD source
        open_usd_build_path: Output directory for OpenUSD install
        wasm_target: Either "wasm32" or "wasm64"
        build_target: Either "template_release" or "template_debug"
    """
    print(f"Building openUSD for WebAssembly ({wasm_target})...")
    
    emsdk_root = env.get('emsdk_root', os.environ.get('EMSDK_ROOT', ''))
    if not emsdk_root:
        Exit("EMSDK_ROOT not configured. Cannot build for WebAssembly.")
    
    openusd_env = os.environ.copy()
    # Add Emscripten to PATH
    em_path = os.path.join(emsdk_root, "upstream", "emscripten")
    openusd_env["PATH"] = f"{em_path}{os.pathsep}{openusd_env.get('PATH', '')}"
    openusd_env["EMSDK"] = emsdk_root
    openusd_env["EMSDK_ROOT"] = emsdk_root

    # Ensure ninja is discoverable for build_usd.py (it defaults to Ninja for wasm targets).
    ninja_name = "ninja.exe" if platform.system() == "Windows" else "ninja"
    ninja_candidates = [
        os.path.join(emsdk_root, "upstream", "emscripten", ninja_name),
        os.path.join(emsdk_root, ninja_name),
    ]
    emsdk_ninja_root = os.path.join(emsdk_root, "ninja")
    if os.path.isdir(emsdk_ninja_root):
        for entry in sorted(os.listdir(emsdk_ninja_root), reverse=True):
            ninja_candidates.append(os.path.join(emsdk_ninja_root, entry, ninja_name))

    # Reuse existing repo-local ninja from vcpkg downloads as fallback.
    vcpkg_tools_root = os.path.abspath(os.path.join("thirdparty", "vcpkg", "downloads", "tools"))
    if os.path.isdir(vcpkg_tools_root):
        for entry in sorted(os.listdir(vcpkg_tools_root), reverse=True):
            ninja_candidates.append(os.path.join(vcpkg_tools_root, entry, ninja_name))

    ninja_path = None
    for candidate in ninja_candidates:
        if os.path.isfile(candidate):
            ninja_path = candidate
            break
    if not ninja_path:
        ninja_path = shutil.which(ninja_name)

    if ninja_path:
        ninja_dir = os.path.dirname(os.path.abspath(ninja_path))
        openusd_env["PATH"] = f"{ninja_dir}{os.pathsep}{openusd_env.get('PATH', '')}"
    else:
        Exit("Ninja not found for OpenUSD wasm build.\n"
             "Install ninja or ensure EMSDK/vcpkg ninja is available.")

    # Try python3 first, fallback to python if not available
    python_cmd = "python3"
    try:
        subprocess.run([python_cmd, "--version"], check=True, capture_output=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        python_cmd = "python"

    # build_usd.py expects 'wasm' (for wasm32) or 'wasm64'.
    build_target_arg = "wasm64" if wasm_target == "wasm64" else "wasm"
    
    # Determine CMake flags for architecture
    if wasm_target == "wasm64":
        memory_flags = "-sWASM64=1"
    else:
        memory_flags = ""

    cmake_args = [
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        "-DCMAKE_CXX_STANDARD=17",
        "-DCMAKE_CXX_FLAGS=\"-pthread --use-port=zlib -fexceptions\"",
        "-DCMAKE_EXE_LINKER_FLAGS=\"-sALLOW_MEMORY_GROWTH=1\"",
        "-DCMAKE_SHARED_LINKER_FLAGS=\"-sALLOW_MEMORY_GROWTH=1\"",
    ]
    if memory_flags:
        cmake_args.append(f"-DCMAKE_CXX_FLAGS_INIT=\\\"{memory_flags}\\\"")

    print(f"Building openUSD for {wasm_target} with build_usd.py --build-target {build_target_arg}...")
    result = subprocess.run([
        python_cmd,
        f"{open_usd_path}/build_scripts/build_usd.py",
        f"{open_usd_build_path}",
        "--verbose",
        "--generator", "Ninja",
        "--build-variant", "release" if build_target == "template_release" else "relwithdebuginfo",
        "--build-monolithic",
        "--no-python",
        "--no-examples",
        "--no-tutorials",
        "--no-tools",
        "--no-debug-python",
        "--no-openvdb",
        "--no-usdview",
        "--no-imaging",
        "--no-vulkan",
        "--no-materialx",
        "--no-docs",
        "--onetbb",
        "--build-target", build_target_arg,
        "--cmake-build-args", " ".join(cmake_args),
    ], env=openusd_env)
    
    if result.returncode != 0:
        print(f"Failed to build openUSD for WebAssembly")
        Exit(f"Build aborted due to subprocess failure (exit code: {result.returncode})")
    
    print(f"OpenUSD for {wasm_target} built successfully: {open_usd_build_path}")

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
