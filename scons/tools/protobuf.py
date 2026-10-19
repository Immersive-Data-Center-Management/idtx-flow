"""
SCons tool: protobuf

Downloads and builds Google Protocol Buffers (protobuf) from source using CMake,
then generates C++ sources for the IDTX `idtxcore.*` wire messages and makes the
protobuf runtime + generated sources available to the GDExtension build.

This mirrors the pattern used by `scons/ixwebsocket.py`:
  - Pinned release + SHA256, downloaded into thirdparty/
  - Built as a STATIC library with the same MSVC runtime (/MT) / C++20 settings
    used by the rest of the extension so the ABI matches.

We pin protobuf v3.21.12 — the last release of the classic "3.x" line that does
NOT require a separate Abseil dependency, which keeps the build simple and avoids
symbol clashes with any Abseil that OpenUSD may vendor. It provides both the
`libprotobuf`/`libprotobuf-lite` runtime and the `protoc` compiler.

Android is cross-compiled: the NDK toolchain (via `scons.platforms.android`)
produces an arm64 `libprotobuf.a`, but protoc built for arm64 cannot run on the
host. So the Android path does *two* builds — a native host build (into
`paths.protobuf_build_host`) purely for a runnable protoc, plus the arm64 target
build (with `protobuf_BUILD_PROTOC_BINARIES=OFF`) for the runtime lib we link.
Native targets (windows/macos/linux) keep a single host==target build.

Usage in SConstruct:
    env.BuildProtobuf()                      # download + build runtime + protoc
    env.GenerateProtoSources(proto_dir, out) # run protoc --cpp_out

After BuildProtobuf(), the following keys are set on the env:
    env['protobuf_include_dir']   - include path for generated headers + runtime
    env['protobuf_lib_dir']       - directory containing libprotobuf(.lib/.a)
    env['protobuf_lib_name']      - link name ("libprotobuf" / "protobuf")
    env['protoc_path']            - absolute path to the built protoc executable
"""
import os
import shutil

from SCons.Script import Exit

from scons.core import cmake_runner
from scons.core.download_utils import download_file, extract_archive
from scons.core.run import run_or_exit
from scons.platforms import android as android_platform

# protobuf 3.21.12 (a.k.a. protoc 21.12) — no Abseil dependency.
PROTOBUF_VERSION = "21.12"
PROTOBUF_SHA256 = "4eab9b524aa5913c6fffb20b2a8abf5ef7f95a80bc0701f3a6dbb4c607f73460"
BASE_URL = "https://github.com/protocolbuffers/protobuf/releases/download"


def generate(env):
    env.AddMethod(_build_protobuf, "BuildProtobuf")
    env.AddMethod(_generate_proto_sources, "GenerateProtoSources")


def exists(env):
    return True


def _expected_lib_file(platform, build_dir):
    """Path to the static libprotobuf produced by the CMake build.

    Windows uses the MSVC multi-config layout (Release/libprotobuf.lib). Every
    other target — including the Android NDK cross-build, which uses the Ninja
    single-config generator — uses the GNU layout (libprotobuf.a in build_dir).
    """
    if platform.name == "windows":
        # protobuf appends 'd' to the debug lib name via CMAKE_DEBUG_POSTFIX; we
        # build Release only (see below) so the name is libprotobuf.lib.
        return os.path.join(build_dir, "Release", "libprotobuf.lib")
    return os.path.join(build_dir, "libprotobuf.a")


def _host_is_windows():
    return os.name == "nt"


def _expected_protoc(build_dir):
    """Path to the protoc executable in the given (host) build dir.

    protoc always runs on the host that drives the build, so even for the
    Android target this resolves against a native build dir — never the arm64
    cross-build. The layout therefore follows the host OS: the Visual Studio
    multi-config generator on Windows puts it under Release/, single-config
    generators put it directly in build_dir.
    """
    if _host_is_windows():
        return os.path.join(build_dir, "Release", "protoc.exe")
    return os.path.join(build_dir, "protoc")


def _publish_env(env, platform, src_dir, target_build_dir, protoc_build_dir):
    """Record protobuf locations on the env for the extension build to consume.

    @param target_build_dir  Dir of the libprotobuf we link against (arm64 for
                             Android, native otherwise).
    @param protoc_build_dir  Dir of the host protoc (a separate native build for
                             Android; same as target_build_dir otherwise).
    """
    env["protobuf_include_dir"] = os.path.join(src_dir, "src")
    if platform.is_windows:
        # MSVC multi-config layout + import-lib naming.
        env["protobuf_lib_dir"] = os.path.join(target_build_dir, "Release")
        env["protobuf_lib_name"] = "libprotobuf"
    else:
        # GNU layout: libprotobuf.a in build_dir, linked as -lprotobuf.
        env["protobuf_lib_dir"] = target_build_dir
        env["protobuf_lib_name"] = "protobuf"
    env["protoc_path"] = _expected_protoc(protoc_build_dir)


# Flags shared by every protobuf build (host or cross).
_COMMON_CMAKE_ARGS = [
    "-Dprotobuf_BUILD_TESTS=OFF",
    "-Dprotobuf_BUILD_EXAMPLES=OFF",
    "-Dprotobuf_BUILD_SHARED_LIBS=OFF",
    "-DCMAKE_CXX_STANDARD=20",
    "-DCMAKE_BUILD_TYPE=Release",
]


def _host_cmake_args():
    """Configure args + CMake generator name for a native (host) build.

    Returns (args, generator) where generator is the explicit ``-G`` name when
    one is forced (Windows), else None (CMake's platform default).
    """
    if _host_is_windows():
        return ([
            "-G", "Visual Studio 17 2022",
            "-A", "x64",
            "-Dprotobuf_MSVC_STATIC_RUNTIME=ON",  # /MT to match the extension
            "-DCMAKE_CXX_FLAGS=/MT /std:c++20 /EHsc",
            "-DCMAKE_C_FLAGS=/MT",
        ], "Visual Studio 17 2022")
    # macOS/Linux host: build a universal lib on macOS, plain -fPIC elsewhere.
    import platform as _host
    args = [
        "-DCMAKE_CXX_FLAGS=-std=c++20 -fPIC",
        "-DCMAKE_C_FLAGS=-fPIC",
    ]
    if _host.system() == "Darwin":
        args.append("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64")
    return (args, None)


def _cache_generator(build_dir):
    """Return the CMAKE_GENERATOR recorded in an existing cache, or None."""
    cache = os.path.join(build_dir, "CMakeCache.txt")
    if not os.path.isfile(cache):
        return None
    with open(cache, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if line.startswith("CMAKE_GENERATOR:"):
                return line.split("=", 1)[1].strip()
    return None


def _ensure_clean_generator(build_dir, wanted_generator):
    """Remove a build dir whose cached generator differs from what we need.

    Re-running cmake with a different generator aborts; the stale Visual Studio
    cache left by the old (mislabeled) Android build is the concrete case this
    guards against.
    """
    if wanted_generator is None:
        return
    existing = _cache_generator(build_dir)
    if existing is not None and existing != wanted_generator:
        print(f"Removing stale protobuf build dir "
              f"(generator '{existing}' != '{wanted_generator}'): {build_dir}")
        shutil.rmtree(build_dir, ignore_errors=True)


def _configure_and_build(src_dir, build_dir, extra_args, generator, description):
    _ensure_clean_generator(build_dir, generator)
    cmake_runner.configure(src_dir, build_dir,
                           _COMMON_CMAKE_ARGS + extra_args, description)
    cmake_runner.build(build_dir, description)


def _ensure_source(paths, src_dir):
    if os.path.exists(src_dir):
        return
    print("Downloading protobuf...")
    os.makedirs(paths.thirdparty, exist_ok=True)

    # The source tarball for a release is named protobuf-cpp-<version>.tar.gz
    archive_name = f"protobuf-cpp-3.{PROTOBUF_VERSION}.tar.gz"
    url = f"{BASE_URL}/v{PROTOBUF_VERSION}/{archive_name}"
    archive_path = os.path.join(paths.thirdparty, archive_name)

    download_file(url, archive_path, "protobuf", PROTOBUF_SHA256)
    extract_archive(archive_path, paths.thirdparty)

    extracted_dir = os.path.join(paths.thirdparty, f"protobuf-3.{PROTOBUF_VERSION}")
    if os.path.exists(extracted_dir):
        os.rename(extracted_dir, src_dir)

    os.remove(archive_path)
    print("protobuf downloaded and extracted successfully.")


def _build_host_protoc(src_dir, host_build_dir):
    """Build (or reuse) a native protobuf just to get a runnable protoc."""
    protoc_file = _expected_protoc(host_build_dir)
    if os.path.exists(protoc_file):
        print(f"host protoc already built: {protoc_file}")
        return
    args, generator = _host_cmake_args()
    print("Building host protobuf (for protoc) ...")
    _configure_and_build(
        src_dir, host_build_dir,
        args + ["-Dprotobuf_BUILD_PROTOC_BINARIES=ON"],
        generator, "protobuf-host",
    )
    if not os.path.exists(protoc_file):
        Exit(f"host protobuf build completed but protoc is missing:\n  {protoc_file}")
    print(f"host protoc built successfully: {protoc_file}")


def _build_android_lib(env, platform, src_dir, target_build_dir):
    """Cross-compile the arm64 static libprotobuf.a (no protoc)."""
    lib_file = _expected_lib_file(platform, target_build_dir)
    if os.path.exists(lib_file):
        print(f"protobuf (android) already built: {lib_file}")
        return

    ndk_root  = env.get("android_ndk_root", os.environ.get("ANDROID_NDK_ROOT", ""))
    api_level = env.get("android_api_level", 24)
    toolchain_args = android_platform.cmake_toolchain_args(ndk_root, api_level)

    print("Cross-compiling protobuf for android (arm64, static) ...")
    _configure_and_build(
        src_dir, target_build_dir,
        toolchain_args + [
            "-DCMAKE_CXX_FLAGS=-std=c++20 -fPIC",
            "-DCMAKE_C_FLAGS=-fPIC",
            # protoc for arm64 would be unrunnable on the host; a separate host
            # build provides the protoc we actually invoke.
            "-Dprotobuf_BUILD_PROTOC_BINARIES=OFF",
        ],
        "Ninja", "protobuf-android",
    )
    if not os.path.exists(lib_file):
        Exit(f"android protobuf build completed but lib is missing:\n  {lib_file}")
    print(f"protobuf for android built successfully: {lib_file}")


def _build_protobuf(env):
    profile  = env["profile"]
    platform = profile.platform
    paths    = profile.paths

    src_dir = paths.protobuf_src
    target_build_dir = paths.protobuf_build

    _ensure_source(paths, src_dir)

    # ---- Android: host protoc + cross-compiled arm64 lib -------------------
    if platform.is_android:
        host_build_dir = paths.protobuf_build_host
        _build_host_protoc(src_dir, host_build_dir)
        _build_android_lib(env, platform, src_dir, target_build_dir)
        _publish_env(env, platform, src_dir, target_build_dir, host_build_dir)
        return

    # ---- Native (host == target): one build yields both lib and protoc -----
    lib_file = _expected_lib_file(platform, target_build_dir)
    protoc_file = _expected_protoc(target_build_dir)

    if os.path.exists(lib_file) and os.path.exists(protoc_file):
        print(f"protobuf already built: {lib_file}")
        _publish_env(env, platform, src_dir, target_build_dir, target_build_dir)
        return

    args, generator = _host_cmake_args()
    print(f"Building protobuf for {platform.name}/{env['target']} (static)...")
    _configure_and_build(
        src_dir, target_build_dir,
        args + ["-Dprotobuf_BUILD_PROTOC_BINARIES=ON"],
        generator, f"protobuf ({platform.name})",
    )

    if not os.path.exists(lib_file) or not os.path.exists(protoc_file):
        Exit(
            "protobuf build completed but expected outputs are missing:\n"
            f"  lib:    {lib_file}\n"
            f"  protoc: {protoc_file}"
        )

    print(f"protobuf built successfully: {lib_file}")
    _publish_env(env, platform, src_dir, target_build_dir, target_build_dir)


def _generate_proto_sources(env, proto_dir, out_dir, proto_files=None):
    """
    Run `protoc --cpp_out` on the given .proto files.

    @param proto_dir   Directory containing the .proto files (the import root).
    @param out_dir     Directory to write generated *.pb.{cc,h} into.
    @param proto_files Optional explicit list of .proto filenames (relative to
                       proto_dir). Defaults to all *.proto in proto_dir.
    """
    protoc = env.get("protoc_path")
    if not protoc or not os.path.exists(protoc):
        Exit("protoc not available — call env.BuildProtobuf() before GenerateProtoSources().")

    proto_dir = os.path.abspath(proto_dir)
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)

    if proto_files is None:
        proto_files = [f for f in os.listdir(proto_dir) if f.endswith(".proto")]

    # Skip regeneration if all outputs are newer than their .proto inputs.
    needs_gen = False
    for pf in proto_files:
        stem = os.path.splitext(pf)[0]
        cc = os.path.join(out_dir, f"{stem}.pb.cc")
        h = os.path.join(out_dir, f"{stem}.pb.h")
        src = os.path.join(proto_dir, pf)
        if (not os.path.exists(cc) or not os.path.exists(h) or
                os.path.getmtime(src) > os.path.getmtime(cc)):
            needs_gen = True
            break

    if not needs_gen:
        print(f"Generated protobuf sources up to date in {out_dir}")
        return

    print(f"Generating protobuf C++ sources into {out_dir}...")
    cmd = [
        protoc,
        f"--proto_path={proto_dir}",
        f"--cpp_out={out_dir}",
    ] + [os.path.join(proto_dir, pf) for pf in proto_files]

    run_or_exit(cmd, "protoc code generation")

    print("protobuf C++ sources generated successfully.")