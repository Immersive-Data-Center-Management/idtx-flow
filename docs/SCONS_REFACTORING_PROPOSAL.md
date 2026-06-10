# SCons Build System – Refactoring Proposal

> Status: **Proposal / for review**
> Scope: `SConstruct` and everything under `scons/`
> Branch context: full multi-platform — Windows, Linux, macOS, **Android (NDK cross-compile)**

This document supersedes the earlier Android-free version of this proposal. The defining new constraint is that Android is a first-class target in the current code base, not an afterthought.

---

## 1. Audit of the current structure

### 1.1 File overview

| File | LOC | Role |
|---|---|---|
| `SConstruct` | 155 | Orchestration + host detection + Android NDK setup + tool wiring |
| `scons/download_utils.py` | 40 | Generic download / sha256 / extract |
| `scons/godotcpp.py` | 65 | Fetch + delegate to godot-cpp's own SConstruct |
| `scons/mdlsdk.py` | 94 | Download NVIDIA MDL SDK (skipped on Android) |
| `scons/ixwebsocket.py` | 344 | CMake-build IXWebSocket; OpenSSL probe; vcpkg bootstrap; Android NDK path |
| `scons/openusd.py` | 497 | Two backends: desktop `build_usd.py` + Android CMake; oneTBB Android build; 5 source patches; MSVC vcvars discovery |
| `scons/openusdextension.py` | 181 | `usdGenSchema` codegen + build IDTX USD plugin shared lib |
| `scons/gdextension.py` | 407 | The main GDExtension shared lib + license copy + USD plugin install |
| `scons/idtxflow_ext.py` | 80 | Extension bootstrap static lib |
| `scons/idtxflow_sdk.py` | 120 | Compose final SDK (copy headers + libs) |
| `scons/core/` | empty | placeholder |
| `scons/tools/` | empty | placeholder |

The build pipeline (as wired up by `SConstruct`):

```
fetch godot-cpp ──► build godot-cpp ─────────────────────────────────┐
fetch ixwebsocket ─► build ixws ─────────────────────────────────────┤
clone openusd ─► build usd (no-py) ──────────────────────────────────┤
clone openusd ─► build usd (py) ─► gen schema ─► build usd-ext ──────┤
download mdl-sdk (skipped on Android) ───────────────────────────────┤
build ext-bootstrap ─────────────────────────────────────────────────┤
                                                                     ├──► gdextension ──► compose SDK
```

### 1.2 What works well

- **Pipeline shape is clear** — `SConstruct` reads top-to-bottom as a sequence of build steps, no clever conditionals.
- **One method = one tool file** — the SCons tool/method registration pattern is followed consistently.
- **Checksums are enforced** for downloaded archives.
- **Aggressive short-circuiting** ("already built? skip") for cheap incremental builds.

### 1.3 Pain points that hurt readability and invite drift

1. **C++20 + RTTI/exceptions + per-target opt flags** repeated in 4 files (`SConstruct`, `gdextension.py`, `openusdextension.py`, `idtxflow_ext.py`) with subtle drift (`-g`, `/MT`, etc.).

2. **Library file naming** (`lib`-prefix, `.so`/`.dll`/`.dylib`, `tbb12` vs `tbb.12`) decided in 6 places.

3. **Per-platform LIBS / LINKFLAGS** inlined in 3 tools that all build shared libs.

4. **OpenSSL discovery duplicated** between `ixwebsocket.py` (thorough probe) and `gdextension.py` (simpler probe). They can drift.

5. **Hard-coded `thirdparty/...` paths** as f-strings in 6 files. Same for `addons/IDTXFlow/bin/...`, `usd/build/...`, `thirdparty/onetbb-android` (3 files).

6. **Three near-identical "fetch + checksum + extract + rename" sequences** in `godotcpp.py`, `ixwebsocket.py`, `mdlsdk.py` despite `download_utils.py` existing.

7. **MSVC `vcvars64.bat` discovery lives inside `openusd.py`** — completely unrelated to USD.

8. **Three keys describe the same concept on the env** (`PLATFORM`, `platform_name`, ad-hoc `platform.system() == "Windows"` checks inside tools).

9. **Android handling is woven everywhere** as `if env.get('is_android'): ...` branches in 6+ files. NDK toolchain resolution happens twice (in `SConstruct` for SCons, and in `ixwebsocket.py` / `openusd.py` for CMake). Ninja-from-Android-SDK discovery is duplicated. The 5 OpenUSD source patches sit mixed with build logic. The Android oneTBB cross-build (~90 lines) sits inside `openusd.py` despite being a separate library. Total Android-specific code: ~345 lines spread across 6 files.

### 1.4 Severity assessment

The current code is **not in crisis**. The remaining duplication is modest but real, and most of it is in cross-cutting concerns (flags, paths, naming, OpenSSL, Android NDK) that are exactly the things that bite hardest when adding a new platform (e.g. iOS) or upgrading a toolchain version.

---

## 2. Layout question: `tools/<platform>/` ?

### 2.1 The proposal

```
scons/tools/windows/<tool>.py
scons/tools/linux/<tool>.py
scons/tools/macos/<tool>.py
scons/tools/android/<tool>.py
scons/tools/shared/<tool>.py
```

### 2.2 Recommendation: **don't do it**

**Reason 1 — SCons looks tools up by name, not by path.** Two `openusd.py` files in `tools/windows/` and `tools/linux/` cannot both register as the SCons tool `openusd`. You'd have to dispatch from `SConstruct`, which is exactly what we don't want.

**Reason 2 — The platform-specific portion of each tool is small.** In `gdextension.py` the platform-specific block is ~80 of 407 lines (~20%). Splitting four ways either duplicates the other 80% or leaves stubs so thin the directory split is just ceremony.

**Reason 3 — The dependency graph, not the platform, is the dominant axis.** Each pipeline box is one logical unit whose cohesion is the build step itself.

**Reason 4 — Even Android, the most divergent target, doesn't justify a per-tool split.** `openusd.py` *is* significantly Android-divergent (~280 lines of Android logic). But splitting into `tools/desktop/openusd.py` + `tools/android/openusd.py` doesn't help: the desktop portion is still shared between Windows/Linux/macOS. The split would just be "Android vs. not Android", better expressed as functions in one file.

**When platform folders *would* make sense:** A platform with a *bespoke pipeline* that shares almost nothing with the others — iOS/Xcode, a console SDK, Web/WASM via Emscripten. None of our current four targets qualify.

---

## 3. Recommended layout

Organise by *concern*, with ONE module per platform for platform-specific helpers.

### 3.1 Directory tree

```
SConstruct                              ~50 lines, pure orchestration
scons/
├── core/                               cross-platform helpers — NO env.AddMethod
│   ├── __init__.py
│   ├── platform_info.py                PlatformInfo dataclass — SoT for name/arch/target
│   ├── paths.py                        BuildPaths dataclass — every hard-coded path
│   ├── compiler_flags.py               apply_cxx_baseline(env, info)
│   ├── platform_link.py                apply_shared_lib_link_flags(env, info, lib_name)
│   ├── lib_naming.py                   shared_lib_filename(), static_lib_filename(), tbb_link_name()
│   ├── fetcher.py                      ThirdPartySource(...).ensure()  — generic download pipeline
│   ├── openssl_locator.py              consolidated OpenSSL probe + vcpkg fallback
│   ├── vcpkg.py                        vcpkg bootstrap + install (extracted from ixwebsocket)
│   ├── msvc_env.py                     vswhere + vcvars64 discovery (extracted from openusd)
│   ├── cmake_runner.py                 standardised cmake configure/build/install + error handling
│   └── run.py                          subprocess.run + Exit-on-failure wrapper
│
├── platforms/                          one module per platform — focused, NO env.AddMethod
│   ├── __init__.py
│   ├── windows.py                      MSVC presets, win32 link libs, vcpkg-OpenSSL preset
│   ├── linux.py                        rpath=$ORIGIN, dl/pthread/m, vcpkg-fallback preset
│   ├── macos.py                        CoreFoundation framework, install_name, OSX_ARCHITECTURES
│   └── android.py                      NDK + CMake toolchain + Ninja + env overrides + link flags
│
├── tools/                              SCons tools — only this dir on toolpath
│   ├── godotcpp.py                     ~40 LOC (was 65)
│   ├── mdlsdk.py                       ~50 LOC (was 94)
│   ├── ixwebsocket.py                  ~120 LOC (was 344) — OpenSSL/vcpkg/Android extracted
│   ├── openusd.py                      ~250 LOC (was 497) — desktop + Android backends + patches inside one file
│   ├── usd_extension.py                ~100 LOC (was 181)  (renamed from openusdextension)
│   ├── ext_bootstrap.py                ~30 LOC  (was 80)   (renamed from idtxflow_ext)
│   ├── gdextension.py                  ~210 LOC (was 407)
│   └── sdk_composer.py                 ~80 LOC  (was 120)  (renamed from idtxflow_sdk)
│
└── download_utils.py                   kept (low-level, used by core/fetcher.py)
```

Three "kinds" of directory:

- **`scons/core/`** — concerns shared across *all* platforms. Plain Python helpers, no `env.AddMethod`.
- **`scons/platforms/`** — one file per platform. Bundles all the platform-specific knowledge that *multiple tools* need.
- **`scons/tools/`** — SCons tools, one per env method. The only directory on `toolpath`.

### 3.2 Why a single `platforms/android.py` rather than a sub-package?

The earlier draft of this proposal suggested an `android/` sub-package with 7-8 small files (`ndk.py`, `cmake_toolchain.py`, `ninja.py`, `flags.py`, `link.py`, `onetbb.py`, `openusd_patches.py`). That was overkill. Each of those would have been 15–30 LOC — **splitting 30 lines into its own file is ceremony, not architecture.**

A single `android.py` of ~120 LOC keeps cohesion high and import statements short. The OpenUSD-Android specifics (the 5 source patches, the CMake-direct USD backend) stay **inside `tools/openusd.py`** because they are only used by that one tool.

The single-file Android module should only be split if it crosses ~300 LOC, *or* if a genuinely independent concern arrives (Android signing, Java stub generation, Gradle integration), *or* if a second mobile target lands and they share infrastructure that should be lifted to `platforms/mobile/`.

### 3.3 What goes where: mapping today → tomorrow

| Concern | Today | Tomorrow |
|---|---|---|
| Platform / arch / target detection | inline in `SConstruct` | `core/platform_info.py` (one `PlatformInfo` value object) |
| `lib`/`.so`/`.dll` naming | inline in 6 files | `core/lib_naming.py` |
| C++20 + RTTI + opt flags | inline in 4 files | `core/compiler_flags.py` |
| Linux/macOS/Windows linker flags + system libs | inline in `gdextension.py`, partly in 2 others | `core/platform_link.py` |
| OpenSSL probe | duplicated in `ixwebsocket.py` and `gdextension.py` | `core/openssl_locator.py` |
| Hard-coded `thirdparty/...` paths | f-strings in 6 files | `core/paths.py` |
| MSVC env discovery | inside `openusd.py` (mis-located) | `core/msvc_env.py` |
| Download → checksum → extract → rename | repeated 3× | `core/fetcher.py` |
| CMake invocation pattern | inline in `ixwebsocket.py` and `openusd.py` (Android branch) | `core/cmake_runner.py` |
| Android NDK paths + host tag + env overrides | inline in `SConstruct` (~30 LOC) | `platforms/android.py` |
| Android `--target=...`, `-march=armv8-a` | inline in `SConstruct` | `platforms/android.py` |
| Android CMake toolchain args | duplicated in `ixwebsocket.py` + `openusd.py` | `platforms/android.py` |
| Android Ninja-from-SDK discovery | duplicated in `ixwebsocket.py` + `openusd.py` | `platforms/android.py` |
| Android oneTBB cross-build | inside `openusd.py` (~90 LOC) | `tools/openusd.py` (kept; only USD uses it) |
| OpenUSD Android source patches + revert | inside `openusd.py` (~110 LOC) | `tools/openusd.py` (kept; only USD uses them) |
| Android-specific link libs | inline in 3 tools | `platforms/android.py` |

Total LOC reduction across `tools/`: roughly **400–500 lines removed**, replaced by ~250 LOC of focused helpers in `core/` + `platforms/`.

### 3.4 Sample module skeletons

> The samples below are illustrative skeletons, not production-ready code.

#### `scons/core/platform_info.py`

```python
from dataclasses import dataclass
import platform

@dataclass(frozen=True)
class PlatformInfo:
    """Single source of truth for the target platform.

    Created once in SConstruct, stashed on env["platform_info"], read by every tool.
    """
    name: str            # "windows" | "linux" | "macos" | "android"
    arch: str            # "x86_64" | "arm64"
    target: str          # "template_debug" | "template_release" | "editor"
    is_msvc: bool        # True only when CXX=cl

    @property
    def is_windows(self) -> bool: return self.name == "windows"
    @property
    def is_linux(self)   -> bool: return self.name == "linux"
    @property
    def is_macos(self)   -> bool: return self.name == "macos"
    @property
    def is_android(self) -> bool: return self.name == "android"

    @property
    def shared_lib_ext(self) -> str:
        return {"windows": "dll", "macos": "dylib",
                "linux": "so", "android": "so"}[self.name]

    @property
    def static_lib_ext(self) -> str:
        return "lib" if self.is_msvc else "a"

    @property
    def shared_lib_prefix(self) -> str:
        return "" if self.is_msvc else "lib"

    @classmethod
    def detect(cls, args) -> "PlatformInfo":
        target_platform = args.get("platform")
        if target_platform is None:
            sys_name = platform.system()
            target_platform = ("windows" if sys_name == "Windows"
                               else "macos" if sys_name == "Darwin"
                               else "linux")
        if target_platform == "android":
            arch = args.get("arch", "arm64")
        else:
            host_arch = platform.machine().lower()
            if host_arch in ("aarch64", "arm64"):
                host_arch = "arm64"
            elif host_arch in ("x86_64", "amd64", "x64"):
                host_arch = "x86_64"
            arch = args.get("arch", host_arch)
        return cls(
            name=target_platform,
            arch=arch,
            target=args.get("target", "template_debug"),
            is_msvc=(target_platform == "windows"),
        )
```

#### `scons/core/paths.py`

```python
from dataclasses import dataclass
from .platform_info import PlatformInfo

@dataclass(frozen=True)
class BuildPaths:
    """Centralised path constants. Renaming a folder is now a one-line change."""
    info: PlatformInfo
    openusd_version: str

    # Third-party sources / installs
    @property
    def thirdparty(self):           return "thirdparty"
    @property
    def godot_cpp(self):            return f"{self.thirdparty}/godot-cpp"
    @property
    def ixwebsocket(self):          return f"{self.thirdparty}/ixwebsocket"
    @property
    def ixwebsocket_build(self):    return f"{self.ixwebsocket}/build_{self.info.name}_{self.info.target}"
    @property
    def mdl_sdk(self):              return f"{self.thirdparty}/mdl_sdk"
    @property
    def vcpkg(self):                return f"{self.thirdparty}/vcpkg"
    @property
    def openusd_src(self):          return f"{self.thirdparty}/openusd-{self.openusd_version}-src"
    @property
    def openusd_install(self):
        if self.info.is_android:
            return f"{self.thirdparty}/openusd-{self.openusd_version}-android"
        return f"{self.thirdparty}/openusd-{self.openusd_version}"
    @property
    def openusd_python(self):       return f"{self.thirdparty}/openusd-{self.openusd_version}-withPython"
    @property
    def onetbb_android(self):       return f"{self.thirdparty}/onetbb-android"

    # IDTX build / install / sdk
    @property
    def gdext_build(self):          return f"build/IDTXFlow/bin/{self.info.name}"
    @property
    def addon_install(self):        return f"addons/IDTXFlow/bin/{self.info.name}"
    @property
    def sdk_root(self):             return "build/idtxflow-sdk"
    @property
    def usd_ext_build(self):        return f"usd/build/{self.info.name}"
    @property
    def usd_ext_libs(self):         return f"usd/libs/{self.info.name}"
    @property
    def usd_ext_include(self):      return "usd/include"
```

#### `scons/core/compiler_flags.py`

```python
def apply_cxx_baseline(env, info):
    """C++20 + RTTI + exceptions + per-target optimisation. Apply once per env."""
    if info.is_msvc:
        env.Append(CXXFLAGS=["/EHsc", "/GR", "/FS", "/std:c++20"])
        if info.target == "template_release":
            env.Append(CCFLAGS=["/O2", "/MT"])
        else:
            env.Append(CCFLAGS=["/Zi", "/Od", "/MT"])
            env.Append(LINKFLAGS=["/DEBUG"])
    else:
        # GCC / Clang / Android NDK Clang
        env.Append(CXXFLAGS=["-fexceptions", "-frtti", "-std=c++20"])
        env.Append(CCFLAGS=["-fPIC"])
        env.Append(CCFLAGS=["-O3" if info.target == "template_release" else "-g"])
```

#### `scons/core/lib_naming.py`

```python
def shared_lib_filename(info, base_name):
    """e.g. base='idtxflow' -> 'libidtxflow.so' / 'libidtxflow.dylib' / 'idtxflow.dll'"""
    return f"{info.shared_lib_prefix}{base_name}.{info.shared_lib_ext}"

def static_lib_filename(info, base_name):
    return f"{info.shared_lib_prefix}{base_name}.{info.static_lib_ext}"

def tbb_link_name(info):
    """OpenUSD ships TBB as 'tbb12' on Windows, 'tbb.12' on Linux/macOS, 'tbb' on Android (separate build)."""
    if info.is_android: return "tbb"
    if info.is_windows: return "tbb12"
    return "tbb.12"
```

#### `scons/core/fetcher.py`

```python
import os
from dataclasses import dataclass
from typing import Optional
from download_utils import download_file, extract_archive

@dataclass(frozen=True)
class ThirdPartySource:
    """Generic 'download → checksum → extract → rename' pipeline.

    Replaces three hand-rolled copies in godotcpp.py, ixwebsocket.py, mdlsdk.py.
    """
    name: str
    url: str
    sha256: str
    archive_filename: str        # e.g. "godot-4.5-stable.tar.gz"
    extract_root: str            # e.g. "thirdparty"
    install_dir: str             # final destination, e.g. "thirdparty/godot-cpp"
    extracted_subdir: Optional[str] = None  # e.g. "godot-cpp-godot-4.5-stable"

    def ensure(self) -> None:
        """No-op if already installed; otherwise download, verify, extract, rename."""
        if os.path.isdir(self.install_dir):
            return
        os.makedirs(self.extract_root, exist_ok=True)
        archive_path = os.path.join(self.extract_root, self.archive_filename)
        download_file(self.url, archive_path, self.name, self.sha256)
        extract_archive(archive_path, self.extract_root)
        if self.extracted_subdir:
            extracted = os.path.join(self.extract_root, self.extracted_subdir)
            if os.path.isdir(extracted):
                os.rename(extracted, self.install_dir)
        if os.path.exists(archive_path):
            os.remove(archive_path)
```

#### `scons/platforms/android.py`

```python
"""Android-specific build helpers.

All the cross-cutting Android knowledge that's needed by MORE THAN ONE tool:
  - NDK root resolution + host tag + toolchain bin paths
  - SCons env overrides (CC/CXX/AR/...) for NDK clang
  - Android-specific compile/link flags (--target=, -march=, link libs, relro/now)
  - CMake toolchain-file args + Ninja-from-SDK discovery (used by ixwebsocket + openusd)
"""
import os
import platform
from SCons.Script import Exit


def resolve_ndk_root(args):
    ndk = args.get("ANDROID_NDK_ROOT", os.environ.get("ANDROID_NDK_ROOT", ""))
    if not ndk or not os.path.isdir(ndk):
        Exit("ANDROID_NDK_ROOT must point to a valid Android NDK directory.\n"
             "  Pass it as:  scons platform=android ANDROID_NDK_ROOT=/path/to/ndk ...")
    return ndk


def host_tag():
    sysname = platform.system().lower()
    if sysname == "windows": return "windows-x86_64"
    if sysname == "darwin":  return "darwin-x86_64"
    return "linux-x86_64"


def toolchain_bin(ndk_root):
    return os.path.join(ndk_root, "toolchains", "llvm", "prebuilt", host_tag(), "bin")


def scons_env_overrides(ndk_root):
    """The CC/CXX/AR/RANLIB/STRIP/AS/LINK overrides for SCons to use NDK clang."""
    bin_dir = toolchain_bin(ndk_root)
    exe = ".exe" if platform.system() == "Windows" else ""
    return {
        "CC":     os.path.join(bin_dir, f"clang{exe}"),
        "CXX":    os.path.join(bin_dir, f"clang++{exe}"),
        "AR":     os.path.join(bin_dir, f"llvm-ar{exe}"),
        "RANLIB": os.path.join(bin_dir, f"llvm-ranlib{exe}"),
        "STRIP":  os.path.join(bin_dir, f"llvm-strip{exe}"),
        "AS":     os.path.join(bin_dir, f"clang{exe}"),
        "LINK":   os.path.join(bin_dir, f"clang++{exe}"),
    }


def apply_compile_flags(env, api_level):
    target_triple = f"aarch64-linux-android{api_level}"
    env.Append(CCFLAGS=[f"--target={target_triple}", "-march=armv8-a"])
    env.Append(LINKFLAGS=[f"--target={target_triple}", "-march=armv8-a"])


def apply_link_flags(env):
    env.Append(LIBS=["log", "android", "dl", "m"])
    env.Append(LINKFLAGS=["-Wl,-z,relro", "-Wl,-z,now"])


def cmake_toolchain_args(ndk_root, api_level):
    """Args passed to `cmake -S ... -B ...` for cross-compiling via the NDK toolchain."""
    return [
        "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={os.path.join(ndk_root, 'build', 'cmake', 'android.toolchain.cmake')}",
        "-DANDROID_ABI=arm64-v8a",
        f"-DANDROID_PLATFORM=android-{api_level}",
        "-DANDROID_STL=c++_shared",
        *_ninja_make_program_arg(),
    ]


def _ninja_make_program_arg():
    """Locate Ninja bundled with the Android SDK so CMake doesn't fall back to MSBuild."""
    sdk = (os.environ.get("ANDROID_HOME")
           or os.environ.get("ANDROID_SDK_ROOT") or "")
    if not sdk: return []
    cmake_dir = os.path.join(sdk, "cmake")
    if not os.path.isdir(cmake_dir): return []
    for entry in sorted(os.listdir(cmake_dir), reverse=True):
        ninja = os.path.join(cmake_dir, entry, "bin",
                             "ninja.exe" if platform.system() == "Windows" else "ninja")
        if os.path.isfile(ninja):
            return [f"-DCMAKE_MAKE_PROGRAM={ninja}"]
    return []
```

#### `SConstruct` after the refactor (~50 lines)

```python
import os
from SCons.Environment import Environment
from SCons.Script import ARGUMENTS

from scons.core.platform_info import PlatformInfo
from scons.core.paths import BuildPaths
from scons.core.compiler_flags import apply_cxx_baseline
from scons.platforms import android

OPENUSD_VERSION = "25.11"

info  = PlatformInfo.detect(ARGUMENTS)
paths = BuildPaths(info=info, openusd_version=OPENUSD_VERSION)

if info.is_android:
    ndk_root  = android.resolve_ndk_root(ARGUMENTS)
    api_level = int(ARGUMENTS.get("android_api_level", "30"))
    env = Environment(
        ENV=os.environ.copy(),
        tools=["gcc", "g++", "gnulink", "ar", "gas",
               "godotcpp", "ixwebsocket", "openusd",
               "usd_extension", "ext_bootstrap",
               "gdextension", "sdk_composer"],   # mdlsdk omitted on Android
        toolpath=["scons/tools"],
        PATH=os.environ.get("PATH", ""),
        **android.scons_env_overrides(ndk_root),
    )
    android.apply_compile_flags(env, api_level)
    env["android_ndk_root"]  = ndk_root
    env["android_api_level"] = api_level
else:
    env = Environment(
        ENV=os.environ.copy(),
        tools=["default",
               "godotcpp", "mdlsdk", "ixwebsocket", "openusd",
               "usd_extension", "ext_bootstrap",
               "gdextension", "sdk_composer"],
        toolpath=["scons/tools"],
        MSVC_VERSION="14.3",
        PATH=os.environ.get("PATH", ""),
    )

env["platform_info"]   = info
env["build_paths"]     = paths
env["openusd_version"] = OPENUSD_VERSION
env["platform_name"]   = info.name      # back-compat for godot-cpp's SConstruct
env["target"]          = info.target
env["arch"]            = info.arch

apply_cxx_baseline(env, info)

# Build pipeline
env.BuildIXWebSocket()
env.BuildOpenUSD(with_python_support=False)
env.BuildOpenUSD(with_python_support=True)
env.GenerateUsdExtensionCode()
env.BuildUsdExtension()
if not info.is_android:
    env.DownloadMdlSdk()
env = env.BuildGodotCPP()
env.BuildExtBootstrapLib()
env.BuildGdExtension()
env.ComposeIdtxFlowGodotSDK()
```

#### Sample `scons/tools/ext_bootstrap.py` after the refactor (~30 lines)

```python
import os
from scons.core.compiler_flags import apply_cxx_baseline
from scons.core.lib_naming    import static_lib_filename


def generate(env): env.AddMethod(_build, "BuildExtBootstrapLib")
def exists(env):   return True


def _build(env):
    info  = env["platform_info"]
    paths = env["build_paths"]

    e = env.Clone()
    e.Append(CPPPATH=[
        "shared/include",
        f"{paths.godot_cpp}/gdextension",
        f"{paths.godot_cpp}/include",
        f"{paths.godot_cpp}/gen/include",
    ])
    apply_cxx_baseline(e, info)
    if info.is_msvc:
        e.Append(CPPDEFINES=["NOMINMAX", "WIN32_LEAN_AND_MEAN"])

    base    = f"idtxflow_ext_bootstrap.{info.name}.{info.arch}"
    out_dir = "build/idtxflow_ext_bootstrap"
    os.makedirs(out_dir, exist_ok=True)

    lib = e.StaticLibrary(f"{out_dir}/{static_lib_filename(info, base)}",
                          ["shared/src/idtxflow_ext/ExtensionBootstrap.cpp"])
    install = e.Install("shared/libs", lib)
    e.Default(lib, install)

    env["ext_bootstrap_lib"]          = static_lib_filename(info, base)
    env["ext_bootstrap_lib_dir"]      = os.path.abspath(out_dir)
    env["ext_bootstrap_library_node"] = lib
    return lib
```

Compare with today's 80-line `idtxflow_ext.py`: the file is now ~30 lines, contains zero platform conditionals (they all live behind `apply_cxx_baseline` and `static_lib_filename`), and the only platform-aware line is the `if info.is_msvc:` for two MSVC-specific defines.

---

## 4. Migration plan (incremental, low-risk)

The refactor can be done in **seven small commits**, each independently reviewable and each leaving the build working. There is no big-bang step.

### Commit 1 — Introduce `core/` skeleton, no behaviour change
- Create `scons/core/__init__.py`, `platform_info.py`, `paths.py`, `lib_naming.py`.
- Create the `PlatformInfo` and `BuildPaths` objects in `SConstruct`, stash on `env`.
- Do **not** yet remove the old `platform_name` / `arch` / `target` / `is_android` keys — both old and new are present, every tool keeps reading the old keys.
- Verify `scons platform=windows` and `scons platform=android` still produce identical artefacts.

### Commit 2 — Extract compiler flags + platform link flags
- Add `core/compiler_flags.py` and `core/platform_link.py`.
- Replace the C++20 baseline in `SConstruct`, `gdextension.py`, `openusdextension.py`, `idtxflow_ext.py` with calls to `apply_cxx_baseline(env, info)`.
- Replace the per-platform `LIBS`/`LINKFLAGS` blocks in `gdextension.py`, `openusdextension.py`, `idtxflow_ext.py` with `apply_shared_lib_link_flags(env, info, install_name)`.
- Verify identical link command lines (`scons -n` diff before/after).

### Commit 3 — Introduce `platforms/android.py`
- Move NDK resolution + host tag + env overrides + compile flags out of `SConstruct` into `platforms/android.py`.
- Move CMake toolchain args + Ninja discovery out of `ixwebsocket.py` and `openusd.py` into `platforms/android.py`.
- Both old call sites now call `android.cmake_toolchain_args(...)` instead of duplicating.
- Verify Android build produces identical artefacts.

### Commit 4 — Extract MSVC env discovery + OpenSSL locator
- Move `_get_windows_msvc_env` from `openusd.py` to `core/msvc_env.py`.
- Create `core/openssl_locator.py` consolidating `_find_system_openssl`, `_probe_openssl_pkg_config`, and `_install_openssl_vcpkg`. Move vcpkg bootstrap into `core/vcpkg.py`.
- Update `ixwebsocket.py` and `gdextension.py` to call into the locator.
- Verify the OpenSSL probe still picks the same paths as before.

### Commit 5 — Generic `ThirdPartySource` fetcher + cmake_runner
- Add `core/fetcher.py` and `core/cmake_runner.py`.
- Convert `godotcpp.py`, `ixwebsocket.py`, `mdlsdk.py` to construct a `ThirdPartySource(...)` and call `.ensure()`.
- Convert CMake invocations in `ixwebsocket.py` and the Android branch of `openusd.py` to `cmake_runner` calls.
- Each of those tools loses ~20 lines of boilerplate.

### Commit 6 — Move and rename tool files
- `git mv scons/godotcpp.py scons/tools/godotcpp.py` … and the other 7 tools.
- Rename `openusdextension.py → usd_extension.py`, `idtxflow_ext.py → ext_bootstrap.py`, `idtxflow_sdk.py → sdk_composer.py`.
- Update `tools=[...]` and `toolpath=["scons/tools"]` in `SConstruct`.
- The method-name registrations (`BuildGdExtension`, `BuildOpenUSD`, etc.) do **not** need to change — only filenames.

### Commit 7 — Drop legacy keys, switch to `BuildPaths` everywhere
- Remove `platform_name` / `target` / `arch` / `is_android` reads from tools, replace with `env["platform_info"]` and `env["build_paths"]`.
- Note: `godot-cpp`'s own `SConstruct` still expects `platform`, `target`, `arch` on the env it's handed — keep those keys, they're its public contract.
- Replace every f-string `thirdparty/...` with the corresponding `paths.<property>` access.
- Final result: each tool file is ~30–60% shorter than today, contains no `if platform_name == ...` ladders, and has zero hard-coded `thirdparty/...` strings.

Each commit is independently testable on **all four target platforms** (Windows, Linux, macOS, Android). If any commit breaks the build, it can be reverted in isolation without rolling back the rest.

---

## 5. Trade-offs and risks

### What this proposal *does not* fix

- **The dual OpenUSD build** (with/without Python) still requires two full builds. That's an upstream `build_usd.py` constraint, not something the refactor can fix.
- **OpenUSD source patches for Android** still need to be applied/reverted around each Android build. They live cleanly inside `tools/openusd.py` but they cannot be eliminated until upstream OpenUSD adds Android support.
- **Verbose error handling** — the pattern `if result.returncode != 0: Exit(...)` is repeated everywhere. `core/run.py` will absorb that, but it's a stylistic improvement, not a correctness one.

### Risks

- **Path lookups via property-only `BuildPaths`** create new strings on every access. Negligible at build-config time; mentioned only for completeness.
- **`scons.core` import** requires `scons/__init__.py`. SCons doesn't pick up `scons/__init__.py` as a tool because tools must define `generate(env)`, but the empty `__init__.py` is needed for Python to treat the directory as a package.
- **`scons/tools/` must NOT have an `__init__.py`** — that directory is on SCons' `toolpath`, not on Python's `sys.path`. Adding one is harmless but misleading.
- **Backwards-compat keys** (`platform_name`, `target`, `arch`) must stay on the env for as long as `godot-cpp/SConstruct` consumes them. They're cheap to keep.
- **The 5 OpenUSD source patches** are tightly coupled to specific OpenUSD versions and NDK versions. Any version bump may invalidate them. A small NDK/USD-version compatibility table inside `tools/openusd.py` (as a comment) would help future maintainers.

### About `__init__.py` files

Required for `scons/core/`, `scons/platforms/` so they work as Python packages and IDE/type-checkers recognise them. **Not required** for `scons/` itself (SCons does its own discovery) and **must not** be added to `scons/tools/` (which is loaded by SCons' tool machinery, not by Python's import system). Since Python 3.3, namespace packages (PEP 420) would make even the `core/` and `platforms/` ones technically optional, but having them eliminates a class of tooling false-positives at zero cost.

---

## 6. Looking ahead: how this absorbs iOS (and other platforms)

When iOS is added on this layout, the changes are localised:

- `core/platform_info.py` — add `is_ios` and `"ios"` value to `name`. ~3 lines.
- `core/paths.py` — add iOS-specific paths if needed. A few properties.
- `core/compiler_flags.py` — add an iOS branch in `apply_cxx_baseline` if the flags differ. Likely 5–10 lines.
- **New** `platforms/ios.py` — Xcode SDK paths, framework search, codesigning if needed. The shape mirrors `platforms/android.py`.
- **Tools stay one-per-step.** Each tool gets at most a small `if info.is_ios:` branch. `tools/openusd.py` may grow another `_build_for_ios()` function next to the existing desktop and Android backends.

If iOS turns out to use Xcode projects (no SCons direct compilation), it gets its own pipeline at `tools/ios/` (genuinely bespoke pipeline — see §2.2). At that point the per-platform-folder pattern earns its keep, but only for iOS and only because its pipeline is fundamentally different.

---

## 7. Summary

- **Don't reorganise by `tools/<platform>/`.** It fights SCons conventions, fragments the natural per-step units, and would force dispatch logic into `SConstruct`.
- **Reorganise by concern** instead: a thin `SConstruct`, a `scons/core/` of focused helpers (platform info, paths, compiler flags, platform link, lib naming, fetcher, OpenSSL locator, vcpkg, MSVC env, CMake runner), a `scons/platforms/` with **one file per platform** (windows, linux, macos, android — not a sub-package per platform), and a `scons/tools/` of straight-line build steps.
- **Keep platform-specific helpers consolidated.** A single `platforms/android.py` (~120 LOC) is better than splitting Android into 7-8 tiny files. OpenUSD-Android specifics (patches, CMake-direct backend) stay inside `tools/openusd.py` because only that one tool uses them.
- **Migrate in 7 small commits** — no big-bang rewrite. Verify on all four platforms after each commit.
- **Expected savings:** ~400–500 LOC removed across `tools/`, replaced by ~250 LOC of focused helpers. Every cross-cutting concern lives in exactly one place.
