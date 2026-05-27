"""
SCons tool: gdextension
Builds the GDExtension library for IDTXFlow and installs it into the addon directory for use by the Godot editor.

This step requires the OpenUSD SDK, MDL SDK, and IXWebSocket library to be built. This can be done with the
respective SCons tools for those dependencies.

Usage in SConstruct:
    env.BuildGdExtension()
"""
import os
import platform

def generate(env):
    env.AddMethod(_build_extension, 'BuildGdExtension')

def exists(env):
    return True

def _build_extension(env):
    print("Building Godot Extension...")

    godot_cpp_path = "thirdparty/godot-cpp"
    idtxflow_sdk_path = "thirdparty/idtxflow-sdk"
    
    platform_name = env["platform_name"]
    build_target = env["target"]
    build_arch = env["arch"]
    
    extension_env = env.Clone()

    # Include paths
    extension_env.Append(CPPPATH=[
        "source",
        f"{idtxflow_sdk_path}/include",
        f"{idtxflow_sdk_path}/include/idtxflow_ext",  # bootstrap headers
        f"{godot_cpp_path}/gdextension",
        f"{godot_cpp_path}/include",
        f"{godot_cpp_path}/gen/include",
    ])

    # Library paths
    extension_env.Append(LIBPATH=[
        f"{idtxflow_sdk_path}/lib",
        f"{godot_cpp_path}/bin",
    ])

    libs = [
        "usd_ms", "tbb12" if platform_name == "windows" else "tbb.12",        
        f"libgodot-cpp.{platform_name}.{build_target}.{build_arch}",
        f"libidtxflow.{platform_name}.{build_target}.{build_arch}",
        f"libidtxflow_ext_bootstrap.{platform_name}.{build_arch}",
    ]

    # generic build flags
    if platform.system() == "Windows" and (env["CXX"] == "cl" or env["CC"] == "cl"):
        extension_env.Append(CXXFLAGS=['/EHsc', '/GR', '/FS', '/arch:AVX2', '/std:c++20'])        
    else:
        extension_env.Append(CXXFLAGS=['-fexceptions', '-frtti', '-g', '-std=c++20'])
        extension_env.Append(CCFLAGS=["-O3" if build_target == "template_release" else "-g"])

    extension_env.Append(CPPDEFINES=["IDTXFLOWEXTENSION_ENABLED", "THREADS_ENABLED", "GDEXTENSION"])

    # Platform-specific configuration
    if platform_name == "linux":
        extension_env.Append(LIBS=libs + ["dl", "pthread", "m"])
        extension_env.Append(CCFLAGS=["-fPIC", "-g", "-frtti"])
        extension_env.Append(LINKFLAGS=["-Wl,-rpath,$ORIGIN"])

    elif platform_name == "windows":
        extension_env.Append(LIBS=libs + ["advapi32", "shell32", "ole32"])
        extension_env.Append(CPPDEFINES=["NOMINMAX", "WIN32_LEAN_AND_MEAN", "_ITERATOR_DEBUG_LEVEL=0"])
        # configure delayed/lazy loading of the dependend dll to generate lazy PE import info
        extension_env.Append(LIBS=["delayimp"])
        extension_env.Append(LINKFLAGS=[f"/DELAYLOAD:libidtxflow.{platform_name}.{build_target}.{build_arch}.dll"])
        if build_target in ["editor", "template_debug"]:
            # DEBUG
            extension_env.Append(CCFLAGS=[
                "/Zi",        # debug symbols
                "/Od",        # no optimization
                "/EHsc",
                "/MT"
            ])
            extension_env.Append(LINKFLAGS=[
                "/DEBUG"      # generate PDB (REQUIRED)
            ])
        else:
            # RELEASE
            extension_env.Append(CCFLAGS=[
                "/O2",
                "/EHsc",
                "/MT"
            ])
    elif platform_name == "macos":
        extension_env.Append(LIBS=libs)
        extension_env.Append(CCFLAGS=["-fPIC", "-g", "-Og", "-O0", "-frtti"])
        extension_env.Append(LINKFLAGS=["-framework", "CoreFoundation"])
        extension_env.Append(LINKFLAGS=["-install_name", "@rpath/libmyidtxflowextension.dylib", "-Wl,-rpath,@loader_path"])
        extension_env.Append(LINKFLAGS=["-g"])        

    # Source files
    sources = list(set(
        extension_env.Glob("source/*.cpp") +
        extension_env.Glob("source/**/*.cpp")
    ))
    # filter the source files in the gen subfolder
    exclude_dir = os.path.normpath("source/gen")
    try:
        sources = [s for s in sources if not os.path.commonpath([s.get_dir().get_path(), exclude_dir]) == exclude_dir]
    except ValueError:
        # Handle case where paths are on different drives - just exclude by simple path check
        sources = [s for s in sources if exclude_dir not in s.get_dir().get_path()]
    
    if build_target in ["editor", "template_debug"]:
        print("Generating doc data..")
        try:
            doc_data = extension_env.GodotCPPDocData("source/gen/doc_data.gen.cpp", source=extension_env.Glob("doc_classes/*.xml"))
            sources.append(doc_data)
        except AttributeError as e:
            print(f"Not including class reference as we're targeting a pre-4.3 baseline. Error: {e}")


    # Output library name
    library_name = f"libmyidtxflowextension.{platform_name}.{build_target}.{build_arch}"
    if platform_name == "windows":
        library_name += ".dll"
    elif platform_name == "macos":
        library_name += ".dylib"
    else:
        library_name += ".so"

    # Set build directory
    build_dir = f"build/MyIDTXFlowExtension/bin/{platform_name}"
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)

    # Build the library
    library = extension_env.SharedLibrary(f"{build_dir}/{library_name}", sources)

    # Determine PDB path
    pdb_file = None
    if platform_name == "windows" and build_target in ["editor", "template_debug"]:
        dll_path = library[0].abspath
        pdb_file = os.path.splitext(dll_path)[0] + ".pdb"

    # Add install target
    install_dir = f"addons/MyIDTXFlowExtension/bin/{platform_name}"
    install_targets = library
    if pdb_file and os.path.exists(pdb_file):
        install_targets.append(extension_env.File(pdb_file))

    install_ext = extension_env.Install(install_dir, install_targets)
    
    extension_env.Default(library, install_ext)
