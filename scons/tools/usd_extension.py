"""SCons tool: usd_extension (refactored)

Generates the IDTX USD schema code via ``usdGenSchema`` and builds the
resulting plugin shared library. Renamed from the legacy
``openusdextension``.

Reads everything from ``env["profile"]`` (a ``BuildProfile``).
Registers ``GenerateUsdExtensionCodeV2`` and ``BuildUsdExtensionV2``.
"""
import os
import platform as _stdlib_platform   # for usdGenSchema.cmd vs usdGenSchema lookup
import shutil
import subprocess
import sys
import sysconfig

from SCons.Script import Exit

from scons.core.apply import apply_profile_section


def generate(env):
    env.AddMethod(_generate_code, "GenerateUsdExtensionCodeV2")
    env.AddMethod(_build_extension, "BuildUsdExtensionV2")


def exists(env):
    return True


# ---------------------------------------------------------------------------
# Code generation (usdGenSchema)
# ---------------------------------------------------------------------------

def _generate_code(env):
    """Run ``usdGenSchema schema.usda ../generated`` against the python build."""
    print("Generate openUSD Extension code from schema...")

    paths = env["profile"].paths
    openusd_root = os.path.abspath(paths.openusd_python)

    gen_env = os.environ.copy()
    gen_env["USD_ROOT"]   = openusd_root
    gen_env["PYTHONPATH"] = f"{openusd_root}/lib/python"
    gen_env["PATH"] = f"{openusd_root}/bin{os.pathsep}{openusd_root}/lib{os.pathsep}{os.environ.get('PATH', '')}"

    bin_dir = f"{openusd_root}/bin"
    cmd_name = "usdGenSchema.cmd" if _stdlib_platform.system() == "Windows" else "usdGenSchema"
    genschema_cmd = os.path.join(bin_dir, cmd_name)

    result = subprocess.run(
        [genschema_cmd, "schema.usda", "../generated"],
        cwd=os.path.abspath(f"{paths.usd_ext_root}/source"),
        env=gen_env,
    )
    if result.returncode != 0:
        Exit(f"openUSD extension code generation failed (exit code: {result.returncode})")


# ---------------------------------------------------------------------------
# Build the IDTX USD plugin shared library
# ---------------------------------------------------------------------------

def _build_extension(env):
    print("Building USD Extensions...")

    profile  = env["profile"]
    platform = profile.platform
    paths    = profile.paths

    openusd_root = os.path.abspath(paths.openusd_install)
    extension_env = env.Clone()

    # Android needs my_spawn from godot-cpp/tools to bypass cmd.exe length limits
    if platform.is_android and (sys.platform == "win32" or sys.platform == "msys"):
        sys.path.insert(0, f"{paths.godot_cpp}/tools")
        try:
            import my_spawn  # type: ignore
            my_spawn.configure(extension_env)
        except ImportError:
            pass

    # Python.h is needed because the OpenUSD withPython headers transitively
    # include it.
    python_include = sysconfig.get_path("include")

    include_paths = [
        paths.usd_ext_generated,
        f"{openusd_root}/include",
        python_include,
    ]
    lib_paths = [f"{openusd_root}/lib"]

    if platform.is_android:
        include_paths.append(f"{paths.onetbb_android}/include")
        lib_paths.append(f"{paths.onetbb_android}/lib")

    extension_env.Append(CPPPATH=include_paths)
    extension_env.Append(LIBPATH=lib_paths)

    # Apply pre-computed profile sections (replaces the four duplicated
    # if-MSVC/else/Android ladders that used to live here).
    apply_profile_section(extension_env, profile.cxx_flags)
    apply_profile_section(extension_env, profile.shared_lib_flags)
    apply_profile_section(extension_env, profile.msvc_extras)

    extension_env.Append(LIBS=["usd_ms", profile.tbb_link_name])
    extension_env.Append(CPPDEFINES=["IDTX_EXPORTS"])

    install_name = profile.shared_lib_filename("idtx_usd")
    if platform.is_macos:
        # macOS install_name needs the per-library filename — the profile
        # can't bake it in because each shared library has a different one.
        extension_env.Append(LINKFLAGS=["-install_name", f"@rpath/{install_name}"])

    # Source files (excluding python wrap_*.cpp)
    sources = list(extension_env.Glob(
        f"{paths.usd_ext_generated}/*.cpp",
        exclude=f"{paths.usd_ext_generated}/wrap*.cpp",
    ))

    build_dir = paths.usd_ext_build
    os.makedirs(build_dir, exist_ok=True)

    output_path = f"{build_dir}/{install_name}"
    shared_library = extension_env.SharedLibrary(output_path, sources)

    install_header = extension_env.Install(
        f"{paths.usd_ext_include}/idtx",
        extension_env.Glob(f"{paths.usd_ext_generated}/*.h"),
    )
    install_libs = extension_env.Install(paths.usd_ext_libs, shared_library)

    extension_env.Default(shared_library, install_header + install_libs)
    extension_env.AddPostAction(shared_library, _copy_plugin_files)


def _copy_plugin_files(target, source, env):
    """Copy USD plugin metadata next to the generated headers."""
    paths = env["profile"].paths
    target_dir = f"{paths.usd_ext_root}/plugin/idtx/resources"
    src_dir    = paths.usd_ext_generated
    shutil.copy(f"{src_dir}/generatedSchema.usda", target_dir)
    shutil.copy(f"{src_dir}/plugInfo.json", target_dir)