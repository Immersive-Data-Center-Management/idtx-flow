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
import platform
import subprocess

from SCons.Script import Exit

from download_utils import download_file, extract_archive

# protobuf 3.21.12 (a.k.a. protoc 21.12) — no Abseil dependency.
PROTOBUF_VERSION = "21.12"
PROTOBUF_SHA256 = "4eab9b524aa5913c6fffb20b2a8abf5ef7f95a80bc0701f3a6dbb4c607f73460"
BASE_URL = "https://github.com/protocolbuffers/protobuf/releases/download"


def generate(env):
    env.AddMethod(_build_protobuf, "BuildProtobuf")
    env.AddMethod(_generate_proto_sources, "GenerateProtoSources")


def exists(env):
    return True


def _protobuf_paths(env):
    """Return (src_dir, build_dir) for the current platform/target."""
    src_dir = os.path.abspath("thirdparty/protobuf")
    platform_name = env["platform_name"]
    build_target = env["target"]
    build_dir = os.path.join(src_dir, f"build_{platform_name}_{build_target}")
    return src_dir, build_dir


def _expected_lib_file(env, build_dir):
    """Path to the static libprotobuf produced by the CMake build."""
    platform_name = env["platform_name"]
    if platform_name == "windows":
        # protobuf appends 'd' to the debug lib name via CMAKE_DEBUG_POSTFIX; we
        # build Release only (see below) so the name is libprotobuf.lib.
        return os.path.join(build_dir, "Release", "libprotobuf.lib")
    return os.path.join(build_dir, "libprotobuf.a")


def _expected_protoc(env, build_dir):
    platform_name = env["platform_name"]
    if platform_name == "windows":
        return os.path.join(build_dir, "Release", "protoc.exe")
    return os.path.join(build_dir, "protoc")


def _publish_env(env, src_dir, build_dir):
    """Record protobuf locations on the env for the extension build to consume."""
    platform_name = env["platform_name"]
    env["protobuf_include_dir"] = os.path.join(src_dir, "src")
    env["protobuf_lib_dir"] = (
        os.path.join(build_dir, "Release") if platform_name == "windows" else build_dir
    )
    env["protobuf_lib_name"] = "libprotobuf" if platform_name == "windows" else "protobuf"
    env["protoc_path"] = _expected_protoc(env, build_dir)


def _build_protobuf(env):
    src_dir, build_dir = _protobuf_paths(env)
    platform_name = env["platform_name"]

    # ---- download + extract ------------------------------------------------
    if not os.path.exists(src_dir):
        print("Downloading protobuf...")
        os.makedirs("./thirdparty", exist_ok=True)

        # The source tarball for a release is named protobuf-cpp-<version>.tar.gz
        archive_name = f"protobuf-cpp-3.{PROTOBUF_VERSION}.tar.gz"
        url = f"{BASE_URL}/v{PROTOBUF_VERSION}/{archive_name}"
        archive_path = os.path.join("./thirdparty", archive_name)

        download_file(url, archive_path, "protobuf", PROTOBUF_SHA256)
        extract_archive(archive_path, "./thirdparty")

        extracted_dir = os.path.join("./thirdparty", f"protobuf-3.{PROTOBUF_VERSION}")
        if os.path.exists(extracted_dir):
            os.rename(extracted_dir, src_dir)

        os.remove(archive_path)
        print("protobuf downloaded and extracted successfully.")

    lib_file = _expected_lib_file(env, build_dir)
    protoc_file = _expected_protoc(env, build_dir)

    if os.path.exists(lib_file) and os.path.exists(protoc_file):
        print(f"protobuf already built: {lib_file}")
        _publish_env(env, src_dir, build_dir)
        return

    # ---- cmake configure ---------------------------------------------------
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)

    print(f"Building protobuf for {platform_name}/{env['target']} (static)...")

    cmake_args = [
        "cmake",
        f"-S{src_dir}",
        f"-B{build_dir}",
        "-Dprotobuf_BUILD_TESTS=OFF",
        "-Dprotobuf_BUILD_EXAMPLES=OFF",
        "-Dprotobuf_BUILD_SHARED_LIBS=OFF",
        "-Dprotobuf_BUILD_PROTOC_BINARIES=ON",
        "-DCMAKE_CXX_STANDARD=20",
        "-DCMAKE_BUILD_TYPE=Release",
    ]

    if platform_name == "windows":
        cmake_args.extend([
            "-G", "Visual Studio 17 2022",
            "-A", "x64",
            "-Dprotobuf_MSVC_STATIC_RUNTIME=ON",  # /MT to match the extension
            "-DCMAKE_CXX_FLAGS=/MT /std:c++20 /EHsc",
            "-DCMAKE_C_FLAGS=/MT",
        ])
    elif platform_name == "macos":
        cmake_args.extend([
            "-DCMAKE_CXX_FLAGS=-std=c++20 -fPIC",
            "-DCMAKE_C_FLAGS=-fPIC",
            "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64",
        ])
    else:
        cmake_args.extend([
            "-DCMAKE_CXX_FLAGS=-std=c++20 -fPIC",
            "-DCMAKE_C_FLAGS=-fPIC",
        ])

    result = subprocess.run(cmake_args, cwd=os.getcwd())
    if result.returncode != 0:
        Exit(f"protobuf CMake configuration failed (exit code: {result.returncode})")

    # ---- cmake build -------------------------------------------------------
    build_cmd = [
        "cmake",
        "--build", build_dir,
        "--config", "Release",
        "--parallel",
    ]
    result = subprocess.run(build_cmd, cwd=os.getcwd())
    if result.returncode != 0:
        Exit(f"protobuf build failed (exit code: {result.returncode})")

    if not os.path.exists(lib_file) or not os.path.exists(protoc_file):
        Exit(
            "protobuf build completed but expected outputs are missing:\n"
            f"  lib:    {lib_file}\n"
            f"  protoc: {protoc_file}"
        )

    print(f"protobuf built successfully: {lib_file}")
    _publish_env(env, src_dir, build_dir)


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

    result = subprocess.run(cmd, cwd=os.getcwd())
    if result.returncode != 0:
        Exit(f"protoc code generation failed (exit code: {result.returncode})")

    print("protobuf C++ sources generated successfully.")