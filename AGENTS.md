# AGENTS.md

IDTX Flow is a **Godot 4.5+ GDExtension** (C++20, SCons) that imports OpenUSD stages
(`.usd/.usda/.usdc/.usdz`) into a Godot scene tree. Conversion is **USD → engine only**
today; a **Godot → USD exporter is the active in-progress feature** (see below).

> **Read [CLAUDE.md](CLAUDE.md) first** — it is the authoritative architecture reference
> (engine boundary, prim dispatch, schemas, plugin/DLL-singleton system, coordinate/units
> handling). This file is the short operational summary; do not duplicate CLAUDE.md, link to it.

## Build & verify

Run from the repo root. First build compiles OpenUSD from source (~40+ min).

```bash
scons                                      # host platform, target=template_debug
scons platform=windows target=template_release -j8
scons platform=android ANDROID_NDK_ROOT=<ndk> target=template_release
scons platform=wasm32  EMSDK_ROOT=<emsdk>     target=template_debug
scons -c                                   # clean (pass the same platform/target args)
```

Outputs land in `addon/IDTXFlow/bin/<platform>/`. `SConstruct` chains the tools in
[scons/](scons/) in a fixed order — change fetch/build logic in `scons/<tool>.py`, not `SConstruct`.

**No automated tests.** Verification is manual: build, copy `addon/IDTXFlow/` into a Godot
project's `addons/`, enable the plugin, add a `UsdStageNode3D`, set its `stage_uri`, inspect
the generated scene tree.

## The engine boundary (most important rule)

The split is physical and compile-enforced:

- `shared/include/idtxflow/` — **engine-agnostic**, header-only C++20 templates parameterized on
  a `TargetEngine` tag type. **Never include `godot_cpp/*` here.** USD parsing and math-neutral
  shaping produce *neutral description structs*.
- `shared/include/idtxflow_godot/` + `source/` — **the only place Godot types appear.** Neutral
  descriptions are turned into `godot::` objects here via `template<>` specializations.

Rule of thumb: USD reading/shaping → `shared/idtxflow/`; anything constructing a `godot::` object
→ `source/` or `idtxflow_godot/`. See CLAUDE.md "Engine-agnostic vs engine-specific".

## Conventions

- **C++20**, PascalCase types, `snake_case` for Godot-bound methods, `UPPER_SNAKE_CASE` macros.
- Namespaces: `idtxflow::{converter,types,helper}` (agnostic), `idtxflow_godot` (Godot).
- Public cross-DLL symbols use the `IDTXFLOW_GODOT_API` macro
  ([idtxflow_godot_api.h](shared/include/idtxflow_godot/idtxflow_godot_api.h)).
- Godot node types live in [source/nodes/](source/nodes/) and mix in `IUsdNode3D`
  ([IUsdNode3D.h](shared/include/idtxflow_godot/nodes/IUsdNode3D.h)) via the
  `IUSDNODE()` / `IUSDNODE_IMPLEMENT_BINDINGS()` macros. `IUsdNode3D` stores prim path/name/type
  and stage path — the metadata that makes **round-trip export** possible.
- Extensions must include `<idtxflow_godot/PrimConverterRegistryGodot.h>` (never the raw
  `<idtxflow/converter/PrimConverterRegistry.h>`) to preserve the single-registry DLL singleton.

## Working on the USD exporter (Godot → USD)

This is a new feature being built incrementally: **transforms → meshes → materials → more**.
Before writing exporter code, read [docs/EXPORTER_DESIGN.md](docs/EXPORTER_DESIGN.md) (the concrete,
phase-by-phase implementation plan) and load the **`usd-exporter`** skill (`.github/skills/usd-exporter/`)
— it maps the existing import pipeline to its inverse and lists the lossy conversions that must be
handled. Mirror the import architecture: neutral export-description structs in `shared/idtxflow/`,
`godot::`-reading extraction in `source/`.

## Key docs

- [CLAUDE.md](CLAUDE.md) — master architecture reference.
- [README.md](README.md) — user guide, supported prim table, material mapping.
- [docs/PLUGIN_ARCHITECTURE.md](docs/PLUGIN_ARCHITECTURE.md) — extension API, registry, macros.
- [docs/DATAFLOW_ARCHITECTURE.md](docs/DATAFLOW_ARCHITECTURE.md) — live dataflow / compute nodes.
- [CONTRIBUTING.md](CONTRIBUTING.md) — DCO sign-off, licensing, AI-generated-code policy.
