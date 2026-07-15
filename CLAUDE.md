# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

IDTX Flow is a **Godot 4.5+ GDExtension** (C++20) that imports OpenUSD stages (`.usd/.usda/.usdc/.usdz`) into a Godot scene tree at edit-time and runtime. Conversion runs one direction only today: **USD → engine**. Targets: Windows, macOS, Android (arm64, beta), Web/WASM (beta).

The core design goal is that the USD-reading logic is **engine-agnostic and reusable** for any engine (Unreal, Unity, …), with Godot being one concrete specialization. Understanding *where the engine boundary is drawn* is the single most important thing for working in this repo — see "Engine-agnostic vs engine-specific" below.

## Build

Everything is driven by SCons, which downloads and compiles OpenUSD from source on first run (~40+ min). Run from the repo root.

```bash
scons                                    # auto-detects host platform, target=template_debug
scons platform=windows target=template_release -j8
scons platform=android ANDROID_NDK_ROOT=<path-to-ndk> target=template_release
scons platform=wasm32  EMSDK_ROOT=<path-to-emsdk> target=template_debug
scons -c                                 # clean (pass same platform/target args)
```

Convenience wrapper (Windows) — edit the `EMSDK_ROOT` / `ANDROID_NDK_ROOT` paths at the top first:

```bat
build_all.bat                 :: incremental, all platforms
build_all.bat windows         :: single platform (debug + release)
build_all.bat windows clean   :: clean then build
```

Build outputs land in `addon/IDTXFlow/bin/<platform>/`. To use the plugin, copy `addon/IDTXFlow/` into a Godot project's `addons/` folder and enable it in Project Settings → Plugins.

`SConstruct` orchestrates a chain of custom tools in `scons/*.py` (order matters): `ixwebsocket` → `openusd` (built twice: once without Python for linking, once with Python to run USD's schema codegen) → `openusdextension` (compiles the custom `idtx` schemas in `usd/`) → `mdlsdk` (skipped on Android/WASM) → `godotcpp` → `idtxflow_ext` (bootstrap lib) → `gdextension` (the extension itself) → `idtxflow_sdk` (packages the extension-author SDK). If you change how a dependency is fetched/built, the relevant `scons/<tool>.py` is where it lives, not `SConstruct`.

There is no automated test suite. Verification is manual: build, then load a USD file via a `UsdStageNode3D` in a Godot project.

## Engine-agnostic vs engine-specific (read this first)

The split is physical, by directory, and enforced by the C++ template system:

- **`shared/include/idtxflow/`** — engine-agnostic. Header-only C++20 templates that read USD and produce *neutral descriptions*. No Godot headers appear here. Everything is parameterized on a `TargetEngine` tag type.
- **`shared/include/idtxflow_godot/`** + **`source/`** — the Godot specialization. This is the only place `godot_cpp/*` is included.

### How the boundary works mechanically

1. **A tag type names the engine.** `types::TargetEngineGodot` (in `GodotTypes.h`) is an empty struct used purely as a template parameter.

2. **A traits struct maps neutral type names to concrete engine types.** `TargetEngineTypes<TargetEngineGodot>` (in `idtxflow_godot/types/GodotTypes.h`) is where the abstract type *names* become concrete Godot types:

   ```cpp
   using Vector3         = godot::Vector3;
   using Transform       = godot::Transform3D;
   using MeshData        = idtxflow::types::MeshData;   // Godot PackedArrays
   using Material        = godot::Ref<godot::StandardMaterial3D>;
   using Texture         = godot::Ref<godot::Texture2D>;
   using ConvertedEntity = godot::Node3D;               // base type every prim converts to
   using OwningEntity    = godot::Node3D;
   ```

   So **"what is a Vector3" is answered here, per engine** — not in the shared code. The shared code only ever refers to `Types::Vector3`. `TargetTypes.h` defines C++20 `concept`s (`Vector3Like`, `ColorLike`, `TransformLike`, …) plus `ValidTargetEngine`, and a `static_assert` verifies Godot's traits satisfy them at compile time. A new engine must provide a `TargetEngineTypes<>` specialization satisfying those concepts.

3. **The shared code declares what it needs; the engine defines it.** Two mechanisms:
   - **Template method specialization** for leaf conversions. `UsdTypeConverter<TargetEngine>::toVector3(GfVec3d)`, `toTransform(...)`, `toMaterial(...)`, `toTexture(...)` are *declared* in `shared/.../TypeConverter.h` and *specialized* for Godot in `shared/include/idtxflow_godot/converter/UsdGodotTypeConverter.h`. This is where a USD `GfVec3d` actually becomes a `godot::Vector3`, a `GfMatrix4d` becomes a `godot::Transform3D`, and USD shader inputs map onto `StandardMaterial3D` channels.
   - **`UsdStageConverter<TargetEngine>`** (in `shared/.../StageConverter.h`) is the orchestrator. It walks the stage, dispatches per prim type, and calls protected `ConvertXform / ConvertCube / ConvertMesh / ConvertSkeleton / ConvertGprimPseudoInstance / ConvertPrimWithPayload / ConvertPrimPostProcess / ConvertStagePostProcess` — all *declared but not defined* in the shared header. Godot's definitions live in `source/converter/UsdGodotStageConverter.h` as explicit `template<>` specializations that `memnew` the actual Godot nodes.

### How a mesh flows through (the MeshConverter example)

`UsdMeshConverter<TargetEngine>` (`shared/.../MeshConverter.h`) does all the USD-side heavy lifting **engine-agnostically**: reads points/faces/normals/UVs/skin weights, triangulates n-gons via fan triangulation, splits `GeomSubset`s into separate surfaces, computes missing normals, limits skin influences to 4 bones. It never touches Godot types directly — it calls:

- `UsdTypeConverter::toVector3/toVector2` to convert each coordinate, and
- `TargetMeshBuilder<TargetEngine>::AddVertex / AddIndex / GetVertexCount` to accumulate results.

`TargetMeshBuilder` is an abstract interface in the shared header; the **Godot specialization** (`TargetMeshBuilder<TargetEngineGodot>` in `UsdGodotTypeConverter.h`) pushes into `MeshData`'s `PackedVector3Array`/`PackedInt32Array` and does the Godot-specific bone-weight normalization (always 4 entries, re-normalized to sum to 1.0). Also engine-specific: `FlipUvV` (Godot flips V).

The converter returns `std::vector<MeshDescription<MeshData>>` (mesh data + bound `UsdShadeMaterial`). Only then does `UsdStageConverter<Godot>::ConvertMesh` (in `source/`) turn that neutral `MeshData` into a `godot::ArrayMesh` with surfaces and materials, wrapped in a `UsdMeshInstanceNode3D`. **Rule of thumb: USD parsing and math-neutral shaping goes in `shared/idtxflow/`; anything that constructs a `godot::` object goes in `source/` or `idtxflow_godot/`.**

Parallel neutral "description" structs exist for the other subsystems: `MaterialDescription`/`MaterialChannel` (`types/MaterialTypes.h`), `AnimationDescription`/tracks (`converter/AnimationConverter.h`), `SkeletonDescription`/bones (`converter/SkeletonConverter.h`). Each is produced engine-agnostically and consumed by a Godot specialization.

### Coordinate system & units

Individual prims are converted **as-authored in USD space** — no per-prim handedness/axis fixups (except the Cone/Cylinder spine-axis rotation baked into `toTransform`'s `spineAxis` argument). The USD→Godot coordinate correction (up-axis Z/X rotation) and meters-per-unit scaling are applied **once per stage** to the root nodes in `ConvertStagePostProcess`, which is far cheaper than transforming every vertex. Nested stages loaded via payload carry `USD_PARENT_UP` / `USD_PARENT_MPU` metadata so the child stage can undo the parent's correction and avoid double-applying it.

## Prim dispatch, extensibility, and node types

`UsdStageConverter::ConvertPrim` dispatches in this order:
1. **Unloaded payload prim** → `ConvertPrimWithPayload` creates a child `UsdStageNode3D` that lazily loads the referenced layer. Authored opinions/overrides on the payload prim are copied into an anonymous `SessionLayer` (serialized into node metadata `USD_OVERRIDE_LAYER`) so relative paths resolve against the referenced stage. (See README "Handling of Payloads".)
2. **Registered custom converter** → `PrimConverterRegistry<TargetEngine>::Get(typeName)`. If a plugin registered an `IPrimConverter` for this prim type, it wins and built-in logic is skipped.
3. **Built-in types** → Material, Xform (+`IDTXInteractionAPI`), Gprim (Cube/Cone/Cylinder/Sphere/Mesh, incl. `IDTXCollisionAPI` colliders and pseudo-instancing), SkelRoot/Skeleton.

**Extension system** (`docs/PLUGIN_ARCHITECTURE.md` is the authoritative guide): third-party GDExtensions add support for new USD prim types *without modifying IDTXFlow*, by implementing `IPrimConverter<TargetEngineGodot>` and calling `PrimConverterRegistry<...>::Instance().Register(...)`. The critical constraint is the **DLL-singleton problem**: the registry must be a single instance living in the IDTXFlow DLL, shared across extension DLLs. This is solved with `extern template` declarations + the `IDTXFLOW_GODOT_API` export macro; explicit instantiation is in `source/converter/PrimConverterRegistryGodot.cpp`. Extension authors must:
- include `<idtxflow_godot/PrimConverterRegistryGodot.h>`, **never** the raw `<idtxflow/converter/PrimConverterRegistry.h>`;
- declare a `[dependencies]` entry in their `.gdextension` so Godot loads IDTXFlow first;
- have their converted nodes inherit `IUsdNode3D` and use the `IUSDNODE()` / `IUSDNODE_IMPLEMENT_BINDINGS()` macros.

A starter is in `templates/extension_template/`. The packaged SDK (headers + import libs + bootstrap lib) is assembled by `scons/idtxflow_sdk.py` (`env.ComposeIdtxFlowGodotSDK()`).

Godot node types (all in `source/nodes/`, all implement the `IUsdNode3D` mixin which carries prim path/type/stage metadata): `UsdStageNode3D` (entry point — set its `stage_uri` to trigger async load+convert), `UsdXFormNode3D`, `UsdMeshInstanceNode3D`, `UsdMultiMeshInstanceNode3D` (pseudo-instancing), `UsdSkeletonNode3D`, `UsdStaticBodyNode3D` (colliders).

## Custom USD schemas (`usd/`)

The repo ships its own codegen'd USD schemas under the `idtx` namespace: `IDTXCollisionAPI`, `IDTXCollisionSetAPI`, `IDTXInteractionAPI` (`usd/generated/`, headers in `usd/include/idtx/`). These are applied API schemas that add collision/interaction metadata to standard prims. They are compiled into `libidtx_usd` by `scons/openusdextension.py` and must be registered with USD's `PlugRegistry` at runtime to resolve.

## Asset resolution & platform plugin registration

- **`source/resolver/`**: `UsdGodotAssetResolver` maps `res://` / `user://` paths into USD's `ArResolver` system; `UsdHttpAssetResolver` fetches `http(s)://` USD files (via IXWebSocket) into `user://usd_cache/`. No auth for remote.
- **Monolithic-build plugin registration** (`source/register_types.cpp`): On desktop, USD auto-discovers its `plugInfo.json` plugin tree. On **Android and Web**, USD is linked as one monolithic library and cannot auto-discover plugins — `ArGetResolver()` would `abort()`. The fix (in `_register_usd_plugins_android` / `_register_usd_plugins_web`) copies the `plugInfo.json` trees from `res://` (inside the PCK) to a writable `user://` location once, then calls `PlugRegistry::RegisterPlugins()` explicitly for both the OpenUSD plugins and the custom `idtx` schema plugins. `IDTXFLOW_USD_PLUGIN_CACHE_VERSION` gates re-extraction. If you touch schemas or the plugin layout, bump that version.

## Adding an exporter (engine → USD) — architectural notes

There is currently **no export path**. The import architecture is a good template to mirror, but export is not simply "run the converters backwards." Notes for when this is picked up:

- **Symmetry to aim for.** Introduce an engine-agnostic `UsdStageExporter<TargetEngine>` in `shared/idtxflow/` that walks a *neutral* scene description and authors USD prims/attributes, plus a `FromTypeConverter<TargetEngine>` with the inverse leaf conversions (`fromVector3(godot::Vector3) → GfVec3f`, `fromTransform`, `fromMaterial`, `fromTexture`). Keep all `pxr::UsdGeom*` authoring calls in the shared layer and all `godot::`-reading in `source/`.

- **The hard asymmetry: there is no `godot::Node3D → USD` traversal in the shared layer today.** Import produces engine types from USD; export must *read* engine types, which the engine-agnostic layer deliberately cannot see. So the Godot side must first extract a neutral description (mirror of `MeshDescription`/`MaterialDescription`/`SkeletonDescription`/`AnimationDescription`) from the scene tree, and the shared exporter consumes only those. Plan the neutral "export description" structs before writing any USD-authoring code.

- **Coordinate/units inversion.** `ConvertStagePostProcess` applies up-axis rotation + MPU scaling once at import. Export must decide the target stage's `upAxis`/`metersPerUnit` and apply the inverse transform — ideally once at the root, matching the import strategy, rather than per-node.

- **Lossy mappings run backwards too.** UV V-flip (`FlipUvV`), bone-weight-to-4 clamping, `StandardMaterial3D`→USD shader-input mapping, and n-gon triangulation are all lossy or one-way. Enumerate which import decisions are reversible; e.g. Godot only has one UV set and one scale/offset per material, so exported USD shaders can't reconstruct multiple USD UV sets that the original import collapsed.

- **Round-trip fidelity vs. authoring clean USD** are different goals — decide which. Preserving original prim paths/types (available via `IUsdNode3D`'s stored prim path/type) enables faithful round-tripping; authoring fresh USD from arbitrary Godot scenes (nodes never imported from USD) is a separate, larger problem.

- **Reuse the registry pattern.** An `IPrimExporter<TargetEngine>` registry mirroring `IPrimConverter` would let extensions export custom node types symmetrically. Node type detection can key off `IUsdNode3D` metadata for imported nodes, and off Godot class for native nodes.

- **`UsdStageNode3D` is the natural export entry point** (e.g. an `export_stage(uri)` method), since it already owns the `pxr::UsdStageRefPtr` and the resolver/cache wiring.
