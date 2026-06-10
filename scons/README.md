# `scons/` — build system layout

This directory holds the SCons build helpers and tools for IDTXFlow.
The build is driven from the workspace-root `SConstruct`, which is
intentionally thin: it constructs a `BuildProfile` and feeds it into a
linear pipeline of tools.

## Three layers, three roles

```
+--------------------------------------------------------------+
|  PROFILE  (scons/core/profile.py)                            |
|  Pure data. A frozen dataclass that names every flag, lib    |
|  preset, install-name decision, NDK toolchain arg, etc.      |
|  Built once at SConstruct startup. Inspect it to know what   |
|  the build will do — no surprises buried in helpers.         |
+--------------------------------------------------------------+
                          |
                          v
+--------------------------------------------------------------+
|  APPLICATOR  (scons/core/apply.py)                           |
|  Mechanical. apply_profile_section(env, section) is one      |
|  line: env.Append(**section). No decisions, no branching.    |
+--------------------------------------------------------------+
                          |
                          v
+--------------------------------------------------------------+
|  SIDE-EFFECTING HELPERS                                      |
|  Procedural; explicit call sites in tools.                   |
|  - core/cmake_runner.py       cmake configure/build/install  |
|  - core/openssl_locator.py    OpenSSL probe (system/brew/    |
|                               pkg-config/vcpkg fallback)     |
|  - core/vcpkg.py              vcpkg bootstrap + install      |
|  - core/msvc_env.py           vswhere + vcvars64 capture     |
|  - core/run.py                subprocess.run + Exit on fail  |
|  - core/fetcher.py            ThirdPartySource(...).ensure() |
+--------------------------------------------------------------+
```

The pattern: **decide once (profile), apply mechanically (apply), do
side-effecting work explicitly (helpers).** Reading any tool, you can
tell at a glance what it changes on the env (just `apply_profile_section`
calls), what it shells out to do (the helpers), and what's specific to
that tool (everything else).

## Directory tree

```
scons/
├── README.md                   this file
├── __init__.py                 package marker so `from scons.core ...` works
├── download_utils.py           low-level download / sha256 / extract
│
├── core/                       cross-platform helpers — NO env.AddMethod
│   ├── __init__.py
│   ├── platform_info.py        PlatformInfo dataclass (target name/arch/...)
│   ├── paths.py                BuildPaths dataclass (every hard-coded path)
│   ├── profile.py              BuildProfile dataclass + build_profile() factory
│   ├── apply.py                apply_profile_section / apply_profile_sections
│   ├── lib_naming.py           shared/static lib filenames + tbb name
│   ├── fetcher.py              ThirdPartySource(...).ensure()
│   ├── openssl_locator.py      consolidated OpenSSL probe + vcpkg fallback
│   ├── vcpkg.py                vcpkg bootstrap + install
│   ├── msvc_env.py             vswhere + vcvars64 capture
│   ├── cmake_runner.py         cmake configure/build/install
│   └── run.py                  subprocess.run + Exit-on-failure wrapper
│
├── platforms/                  per-platform helpers used by multiple tools
│   ├── __init__.py
│   └── android.py              NDK + CMake toolchain + Ninja + env overrides
│
└── tools/                      SCons tools (only directory on toolpath)
    ├── godotcpp.py             -> BuildGodotCPPV2
    ├── mdlsdk.py               -> DownloadMdlSdkV2
    ├── ixwebsocket.py          -> BuildIXWebSocketV2
    ├── openusd.py              -> BuildOpenUSDV2 (incl. Android backend + patches)
    ├── usd_extension.py        -> GenerateUsdExtensionCodeV2 + BuildUsdExtensionV2
    ├── ext_bootstrap.py        -> BuildExtBootstrapLibV2
    ├── gdextension.py          -> BuildGdExtensionV2
    └── sdk_composer.py         -> ComposeIdtxFlowGodotSDKV2
```

## Why no `scons/platforms/{windows,linux,macos}.py` files?

Earlier drafts of the refactor created one tiny module per platform.
After the profile-based design landed, all that code became data inside
`build_profile()` — there was nothing left for those files to hold.
Empty placeholder files were a smell, so they're gone.

`scons/platforms/android.py` survives because Android still has
**procedural** behaviour that doesn't fit into the declarative profile:
NDK root validation (calls `Exit` on failure), Ninja-from-SDK probing,
host-tag computation, CMake toolchain-args construction. Those are
genuinely *helpers* — call sites in `SConstruct` and a couple of tools.

When iOS or another platform with similar procedural needs arrives, it
gets its own `scons/platforms/ios.py` the same way. Platforms whose
divergence is purely flag presets stay invisible — they live as a
branch inside `build_profile()`.

## Why the `V2` method-name suffix on tools?

The legacy tools (in `scons/`, flat) registered methods like
`BuildGodotCPP`, `BuildOpenUSD`, `BuildGdExtension`, etc. While both
sides coexisted during the refactor, the new tools registered V2
variants (`BuildGodotCPPV2`, `BuildOpenUSDV2`, ...) so SCons env method
names didn't collide. The legacy tool files have since been retired,
but the V2 names are kept for now as a marker that these are the
profile-driven implementations.

A future cleanup pass can drop the V2 suffix once the legacy code paths
are removed.

## Why `__init__.py` in some places but not all?

  - `scons/__init__.py` — needed for `from scons.core ...` imports from
    refactored tools. Empty marker.
  - `scons/core/__init__.py`, `scons/platforms/__init__.py` — same
    reason: these directories contain Python modules imported with
    standard `import` statements. Empty markers.
  - `scons/tools/__init__.py` — **deliberately absent.** SCons loads
    files in `toolpath` directories via its own discovery mechanism
    (looking for `generate(env)` + `exists(env)`), not via Python's
    import system. Adding `__init__.py` here would imply package
    semantics that aren't actually used.

## See also

  - `docs/SCONS_REFACTORING_PROPOSAL.md` — full design document
    explaining the *why* behind every choice in this layout.
  - `SConstruct` (workspace root) — the entry point.