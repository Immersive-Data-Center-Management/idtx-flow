import os
import platform
import shutil
import subprocess
import sysconfig
import sys
from SCons.Script import Exit

def generate(env):
    env.AddMethod(_generate_usd_extensions_code, 'GenerateUsdExtensionCode')
    env.AddMethod(_build_usd_extension, 'BuildUsdExtension')

def exists(env):
    return True

#-----------------------------------------------------------------------------------------------------------------------
# Generate the openUSD plugin code based on the provided 'schema.usda' file in the 'usd/source/' folder
#-----------------------------------------------------------------------------------------------------------------------
def _generate_usd_extensions_code(env):
    print("Generate openUSD Extension code from schema...")

    extension_root = f"usd"
    openusd_version = env.get('openusd_version', '')
    openusd_root = os.path.abspath(f"./thirdparty/openusd-{openusd_version}-withPython")
    openusd_env = os.environ.copy()
    openusd_env["USD_ROOT"] = openusd_root
    openusd_env["PYTHONPATH"] = f"{openusd_root}/lib/python"
    openusd_bin_path = f"{openusd_root}/bin"
    openusd_env["PATH"] = f"{openusd_root}/bin{os.pathsep}{openusd_root}/lib{os.pathsep}{os.environ.get("PATH", "")}"

    # On Windows the .cmd wrapper calls the bare `python` command, which may
    # resolve to a different interpreter version than the one used to compile
    # the USD .pyd extension modules.  Read the shebang from the usdGenSchema
    # Python script to get the exact interpreter that was used at build time
    # and invoke that directly.
    genschema_script = os.path.join(openusd_bin_path, "usdGenSchema")
    python_for_genschema = None
    try:
        with open(genschema_script, "r", encoding="utf-8") as _f:
            _first = _f.readline().strip()
        if _first.startswith("#!"):
            _shebang_python = _first[2:].strip()
            if os.path.isfile(_shebang_python):
                python_for_genschema = _shebang_python
    except OSError:
        pass

    if python_for_genschema is None:
        # Fallback: try python3 then python on PATH
        for _candidate in ("python3", "python"):
            try:
                subprocess.run([_candidate, "--version"], check=True, capture_output=True)
                python_for_genschema = _candidate
                break
            except (subprocess.CalledProcessError, FileNotFoundError):
                pass

    if python_for_genschema is None:
        Exit("Could not locate a Python interpreter to run usdGenSchema.")

    result = subprocess.run([
        python_for_genschema,
        genschema_script,
        "schema.usda",
        f"../generated"
    ],
        cwd=os.path.abspath(f"{extension_root}/source"),
        env=openusd_env)

    if result.returncode != 0:
        print(f"Failed to generate openUSD extension code")
        Exit(f"Build aborted due to subprocess failure (exit code: {result.returncode} / {result.stdout} / {result.args}  )")
        
def _build_usd_extension(env):
    print("Building USD Extensions...")

    openusd_version = env.get('openusd_version', '')
    is_android = env.get('is_android', False)
    is_wasm = env.get('is_wasm', False)
    wasm_target = env.get('wasm_target', None)

    # OpenUSD install path differs for Android and Wasm
    if is_android:
        openusd_root = os.path.abspath(f"thirdparty/openusd-{openusd_version}-android")
    elif is_wasm:
        openusd_root = os.path.abspath(f"thirdparty/openusd-{openusd_version}-{wasm_target}")
    else:
        openusd_root = os.path.abspath(f"thirdparty/openusd-{openusd_version}")
    
    extension_root = f"./usd"

    platform_name = env["platform_name"]
    build_target = env["target"]

    extension_env = env.Clone()

    # --> dieses IF fehlte...
    if is_android or is_wasm:
        # this is required to bypass windows cmd.exe limitations when invoking the android/emscripten compiler
        # as seen in the godot-cpp build tools script 
        if sys.platform == "win32" or sys.platform == "msys":
            sys.path.insert(0, "thirdparty/godot-cpp/tools")
            import my_spawn
            my_spawn.configure(extension_env)
        
    # Python include path is needed because the OpenUSD withPython build
    # headers transitively include Python.h (via pySafePython.h / wrap_python.hpp)
    python_include = sysconfig.get_path('include')

    # oneTBB install path for Android (built separately from OpenUSD)
    onetbb_android_root = "thirdparty/onetbb-android"

    # Include paths
    include_paths = [
        f"{extension_root}/generated",
        f"{openusd_root}/include",
        python_include,
    ]
    
    lib_paths = [
        f"{openusd_root}/lib",
    ]
    # Android and Wasm: oneTBB headers and libs are in a separate install prefix
    if is_android:
        include_paths.append(f"{onetbb_android_root}/include")
        lib_paths.append(f"{onetbb_android_root}/lib")

    extension_env.Append(CPPPATH=include_paths)
    extension_env.Append(LIBPATH=lib_paths)

    if is_android:
        libs = [
            "usd_ms", "tbb"
        ]
    elif is_wasm:
        libs = [
            "usd_m", "tbb"
        ]
    else:
        libs = [
            "usd_ms", "tbb12" if platform_name == "windows" else "tbb.12"
        ]

    # generic build flags
    if is_android:
        extension_env.Append(CXXFLAGS=['-fexceptions', '-frtti', '-std=c++20'])
        extension_env.Append(CCFLAGS=["-O3" if build_target == "template_release" else "-g"])
    elif is_wasm:
        # Emscripten/Wasm-specific flags
        extension_env.Append(CXXFLAGS=['-fexceptions', '-frtti', '-std=c++20', '-pthread', '--use-port=zlib', '-fPIC'])
        extension_env.Append(CCFLAGS=["-O3" if build_target == "template_release" else "-g", '-pthread', '-fPIC'])
        extension_env.Append(LINKFLAGS=['-sALLOW_MEMORY_GROWTH=1', '-pthread'])
        if wasm_target == "wasm64":
            extension_env.Append(CXXFLAGS=['-sWASM64=1'])
            extension_env.Append(CCFLAGS=['-sWASM64=1'])
            extension_env.Append(LINKFLAGS=['-sWASM64=1'])
    elif platform.system() == "Windows" and (extension_env["CXX"] == "cl" or extension_env["CC"] == "cl"):
        extension_env.Append(CXXFLAGS=['/EHsc', '/GR', '/FS', '/arch:AVX2'])
        extension_env.Append(CCFLAGS=["/O2" if build_target == "template_release" else "/Zi"])
    else:
        extension_env.Append(CXXFLAGS=['-fexceptions', '-frtti', '-g'])
        extension_env.Append(CCFLAGS=["-O3" if build_target == "template_release" else "-g"])

    # Platform-specific configuration
    if platform_name == "android":
        extension_env.Append(LIBS=libs + ["log", "android", "dl", "m"])
        extension_env.Append(CCFLAGS=["-fPIC", "-frtti"])
        extension_env.Append(LINKFLAGS=["-Wl,-z,relro", "-Wl,-z,now"])

    elif platform_name == "wasm32" or platform_name == "wasm64":
        # WebAssembly: static library linking
        extension_env.Append(LIBS=libs)
        # Wasm is a static library, so no dynamic linker flags needed
        extension_env.Append(CPPDEFINES=["IDTX_EXPORTS"])

    elif platform_name == "linux":
        # Shared library settings
        extension_env.Append(LIBS=libs + ["dl", "pthread", "m"])
        extension_env.Append(CCFLAGS=["-fPIC", "-g", "-frtti"])
        extension_env.Append(LINKFLAGS=["-Wl,-rpath,$ORIGIN"])
        extension_env.Append(CPPDEFINES=["IDTX_EXPORTS"])

    elif platform_name == "windows":
        common_libs = libs
        common_defines = ["NOMINMAX", "WIN32_LEAN_AND_MEAN"]

        # Shared library settings
        extension_env.Append(LIBS=common_libs)
        extension_env.Append(CCFLAGS=["/EHsc", "/MD"])  # Use /MD for shared
        extension_env.Append(CPPDEFINES=common_defines + ["IDTX_EXPORTS"])

    elif platform_name == "macos":
        # Shared library settings
        extension_env.Append(LIBS=libs)
        extension_env.Append(CCFLAGS=["-fPIC", "-g", "-Og", "-O0", "-frtti"])
        extension_env.Append(LINKFLAGS=["-framework", "CoreFoundation"])
        extension_env.Append(LINKFLAGS=["-install_name", "@rpath/libidtx_usd.dylib", "-Wl,-rpath,@loader_path"])
        extension_env.Append(LINKFLAGS=["-g"])
        extension_env.Append(CPPDEFINES=["IDTX_EXPORTS"])

    # Source files excluding the python wrapper files as we do not need them
    sources = list(extension_env.Glob(f"{extension_root}/generated/*.cpp", exclude=f"{extension_root}/generated/wrap*.cpp"))

    # Set build directories
    build_dir = f"{extension_root}/build/{platform_name}"
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)

    # Output library names and build type
    if is_wasm:
        # WebAssembly produces static libraries
        static_lib_name = f"libidtx_usd.a"
        static_library = extension_env.StaticLibrary(f"{build_dir}/{static_lib_name}", sources)
        
        # install/copy the header files to the shared include directory
        include_dest = f"{extension_root}/include/idtx"
        lib_dest = f"{extension_root}/libs/{platform_name}"
        install_header = extension_env.Install(include_dest, extension_env.Glob(f"{extension_root}/generated/*.h"))
        install_libs = extension_env.Install(lib_dest, static_library)

        # Build the extension library and copy the created header files
        extension_env.Default(static_library, install_header + install_libs)
        extension_env.AddPostAction(static_library, _copy_plugin_files)
    else:
        # Desktop and Android produce shared libraries
        shared_lib_name = f"libidtx_usd"
        if platform_name == "windows":
            shared_lib_name += ".dll"
        elif platform_name == "macos":
            shared_lib_name += ".dylib"
        else:
            shared_lib_name += ".so"

        # Build the libraries using their respective environments
        shared_library = extension_env.SharedLibrary(f"{build_dir}/{shared_lib_name}", sources)

        # install/copy the header files to the shared include directory
        include_dest = f"{extension_root}/include/idtx"
        lib_dest = f"{extension_root}/libs/{platform_name}"
        install_header = extension_env.Install(include_dest, extension_env.Glob(f"{extension_root}/generated/*.h"))
        install_libs = extension_env.Install(lib_dest, shared_library)

        # Build the extension library and copy the created header files
        extension_env.Default(shared_library, install_header + install_libs)
        extension_env.AddPostAction(shared_library, _copy_plugin_files)

def _copy_plugin_files(target, source, env):
    source_dir = f"./usd/generated"
    target_dir = "./usd/plugin/idtx/resources"
    shutil.copy(f"{source_dir}/generatedSchema.usda", f"{target_dir}")
    shutil.copy(f"{source_dir}/plugInfo.json",f"{target_dir}")