"""SCons tool: ext_bootstrap (refactored)

Builds the IDTXFlow extension bootstrap static library that downstream
plugins link against. Renamed from the legacy ``idtxflow_ext`` module.

Reads everything from ``env["profile"]`` (a ``BuildProfile``).
Registers the SCons env method ``BuildExtBootstrapLibV2``.
"""
import os

from scons.core.apply import apply_profile_section


def generate(env):
    env.AddMethod(_build, "BuildExtBootstrapLibV2")


def exists(env):
    return True


def _build(env):
    print("Building IDTXFlow Extension Bootstrap Library...")

    profile  = env["profile"]
    platform = profile.platform
    paths    = profile.paths

    bootstrap_env = env.Clone()
    bootstrap_env.Append(CPPPATH=[
        paths.shared_include,
        f"{paths.godot_cpp}/gdextension",
        f"{paths.godot_cpp}/include",
        f"{paths.godot_cpp}/gen/include",
    ])

    apply_profile_section(bootstrap_env, profile.cxx_flags)
    apply_profile_section(bootstrap_env, profile.msvc_extras)
    # Note: NOT applying shared_lib_flags — this is a *static* library.

    base_name = f"idtxflow_ext_bootstrap.{platform.name}.{platform.arch}"
    out_dir   = paths.ext_bootstrap_build
    os.makedirs(out_dir, exist_ok=True)

    library_filename = profile.static_lib_filename(base_name)
    library = bootstrap_env.StaticLibrary(
        f"{out_dir}/{library_filename}",
        ["shared/src/idtxflow_ext/ExtensionBootstrap.cpp"],
    )

    os.makedirs(paths.shared_libs, exist_ok=True)
    install = bootstrap_env.Install(paths.shared_libs, library)

    bootstrap_env.Default(library, install)

    # Stash for the SDK composer
    env["ext_bootstrap_lib"]          = base_name
    env["ext_bootstrap_lib_filename"] = library_filename
    env["ext_bootstrap_lib_dir"]      = os.path.abspath(out_dir)
    env["ext_bootstrap_library_node"] = library

    return library