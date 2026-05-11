"""
SCons tool: idtxflow_ext
Builds the IDTXFlow extension bootstrap static library that plugins, extending IDTXFlow-GDExtension, can link against.

Using the bootstrap library within the IDTXFlow extension ensures, that the dynamic library of the required GDExtension
is loaded and the necessary initialization is performed before any plugin code is executed.

Usage in SConstruct:
    env.BuildExtBootstrapLib()
"""
import os
import platform

def generate(env):
    env.AddMethod(_build_ext_bootstrap_lib, 'BuildExtBootstrapLib')

def exists(env):
    return True

def _build_ext_bootstrap_lib(env):
    print("Building IDTXFlow Extension Bootstrap Library...")

    platform_name = env["platform_name"]
    build_target = env["target"]
    build_arch = env["arch"]

    godot_cpp_path = "thirdparty/godot-cpp"

    bootstrap_env = env.Clone()

    # Include paths — needs godot-cpp headers (for the macro) and our shared headers
    bootstrap_env.Append(CPPPATH=[
        "shared/include",
        f"{godot_cpp_path}/gdextension",
        f"{godot_cpp_path}/include",
        f"{godot_cpp_path}/gen/include",
    ])

    # Compile flags
    if platform.system() == "Windows" and (env["CXX"] == "cl" or env["CC"] == "cl"):
        bootstrap_env.Append(CXXFLAGS=['/EHsc', '/GR', '/std:c++20'])
        bootstrap_env.Append(CPPDEFINES=["NOMINMAX", "WIN32_LEAN_AND_MEAN"])
        if build_target in ["editor", "template_debug"]:
            bootstrap_env.Append(CCFLAGS=["/Zi", "/Od", "/MT"])
        else:
            bootstrap_env.Append(CCFLAGS=["/O2", "/MT"])
    else:
        bootstrap_env.Append(CXXFLAGS=['-fexceptions', '-frtti', '-std=c++20'])
        bootstrap_env.Append(CCFLAGS=["-fPIC"])
        bootstrap_env.Append(CCFLAGS=["-O3" if build_target == "template_release" else "-g"])

    # Source
    sources = ["shared/src/idtxflow_ext/ExtensionBootstrap.cpp"]

    # Output
    library_name = f"libidtxflow_ext_bootstrap.{platform_name}.{build_arch}"
    library_extension = "lib" if platform_name == "windows" else ("a" if platform_name == "macos" else "a")
    
    build_dir = "build/idtxflow_ext_bootstrap"
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)

    library = bootstrap_env.StaticLibrary(f"{build_dir}/{library_name}.{library_extension}", sources)

    # Also install into the SDK libs directory so consumer extensions can find it
    sdk_lib_dir = "shared/libs"
    if not os.path.exists(sdk_lib_dir):
        os.makedirs(sdk_lib_dir)
    install = bootstrap_env.Install(sdk_lib_dir, library)

    bootstrap_env.Default(library, install)

    # Store the library name and node in the environment so idtxflow_sdk.py can reference it
    env['ext_bootstrap_lib'] = library_name
    env['ext_bootstrap_lib_dir'] = os.path.abspath(build_dir)
    env['ext_bootstrap_library_node'] = library

    return library
