---
name: usd-exporter
description: "Use when building or extending the IDTX Flow USD exporter (Godot scene -> OpenUSD), the reverse of the existing USD->Godot importer. Covers exporting transform nodes, meshes, materials, skeletons and animation from Godot back to USD; inverting lossy import conversions (UV V-flip, bone-weight clamp, up-axis/meters-per-unit correction, n-gon triangulation); where to put engine-agnostic vs Godot-specific export code; and round-trip fidelity via IUsdNode3D metadata. Load before writing any exporter (FromTypeConverter, UsdStageExporter, IPrimExporter) code."
---

# IDTX Flow USD Exporter (Godot → USD)

The exporter is **new and being built incrementally**: transforms → meshes → materials → then
skeletons, animation, colliders, custom prims. There is **no export code yet** — mirror the import
architecture rather than inventing a new one. Read [CLAUDE.md](../../../CLAUDE.md) "Adding an
exporter" and "Engine-agnostic vs engine-specific" first.

> **Concrete implementation plan:** [docs/EXPORTER_DESIGN.md](../../../docs/EXPORTER_DESIGN.md) —
> the phased, file-by-file design (locked decisions, neutral structs, Godot extraction, USD
> authoring, editor UI). Follow it literally when implementing; this skill is the conceptual map.

## The one hard asymmetry

Import produces engine types *from* USD. Export must **read** `godot::Node3D` trees — which the
engine-agnostic layer (`shared/include/idtxflow/`) deliberately cannot see. So:

1. The **Godot side** (`source/`) walks the scene tree and extracts *neutral export-description
   structs* (mirroring the import descriptions below).
2. The **engine-agnostic side** (`shared/idtxflow/`) consumes only those neutral structs and
   authors `pxr::UsdGeom*` / `pxr::UsdShade*` prims.

**Plan the neutral export-description structs before writing any USD-authoring code.** Never read a
`godot::` type in `shared/idtxflow/`, never author USD in `source/` — keep the boundary identical
to import.

## Architecture to mirror (symmetry map)

| Import (exists) | Export (to build) | Layer |
|---|---|---|
| `UsdStageConverter<TargetEngine>` (`shared/.../StageConverter.h`) | `UsdStageExporter<TargetEngine>` | agnostic |
| `UsdTypeConverter::toVector3/toTransform/toMaterial/toTexture` (`shared/.../TypeConverter.h`) | `FromTypeConverter::fromVector3/fromTransform/fromMaterial/fromTexture` | agnostic decl, Godot specialization |
| `TargetMeshBuilder<Godot>` (reads neutral → builds `MeshData`) | mesh-*reader* that walks `ArrayMesh` surfaces → neutral `MeshData` | Godot (`source/`) |
| `IPrimConverter<TargetEngineGodot>` registry | `IPrimExporter<TargetEngineGodot>` registry (reuse DLL-singleton pattern) | Godot |
| `UsdStageNode3D` loads a stage | `UsdStageNode3D::export_stage(uri)` — it already owns the `UsdStageRefPtr` + resolver | Godot |

## Neutral description structs (import side, to be reused/mirrored)

These are the contract between the two layers — the exporter should read them, not reinvent them.

- `MeshDescription<MeshData>` — [MeshConverter.h](../../../shared/include/idtxflow/converter/MeshConverter.h) (`meshData`, bound `pxr::UsdShadeMaterial`).
- `MeshData` (Godot) — [GodotTypes.h](../../../shared/include/idtxflow_godot/types/GodotTypes.h): `Vertices`, `Triangles`, `Normals`, `UVs`, `VertexColors`, `Bones`, `Weights`, `boneWeightCount`.
- `MaterialDescription<Texture>` / `MaterialChannel` / `TextureDescription` — [MaterialTypes.h](../../../shared/include/idtxflow/types/MaterialTypes.h). Channels: Diffuse, Metallic, Roughness, Specular, Emissive, Opacity, Normal, AmbientOcclusion, ClearCoat, ORM.
- `SkeletonDescription` / `Bone` — [SkeletonConverter.h](../../../shared/include/idtxflow/converter/SkeletonConverter.h).
- `AnimationDescription` / tracks — [AnimationConverter.h](../../../shared/include/idtxflow/converter/AnimationConverter.h).

## Lossy / one-way import decisions — must be inverted or accepted

The exporter must reverse these; where a decision is lossy, decide round-trip fidelity vs. clean USD.

| Import decision | Where | Export action |
|---|---|---|
| **UV V-flip** `uv.y = -uv.y` (`FlipUvV`) | [MeshConverter.h](../../../shared/include/idtxflow/converter/MeshConverter.h), [UsdGodotTypeConverter.h](../../../shared/include/idtxflow_godot/converter/UsdGodotTypeConverter.h) | Un-flip V on export. |
| **Bone weights clamped to 4/8, re-normalized to 1.0** | `TargetMeshBuilder<Godot>::AddVertex` in [UsdGodotTypeConverter.h](../../../shared/include/idtxflow_godot/converter/UsdGodotTypeConverter.h) | Export the (clamped) weights as-is; original >4 influences are unrecoverable. |
| **StandardMaterial3D ↔ USD shader** mapping; Godot has ONE UV scale/offset per material | `toMaterial` in [UsdGodotTypeConverter.h](../../../shared/include/idtxflow_godot/converter/UsdGodotTypeConverter.h) | Map StandardMaterial3D channels → `UsdPreviewSurface` inputs; cannot reconstruct multiple USD UV sets collapsed on import. |
| **N-gon fan triangulation**; vertices **not** deduplicated | [MeshConverter.h](../../../shared/include/idtxflow/converter/MeshConverter.h) | Export triangles (`faceVertexCounts` all = 3); original polygon topology is lost. |
| **Spine-axis rotation** baked into `toTransform` for Cone/Cylinder | [UsdGodotTypeConverter.h](../../../shared/include/idtxflow_godot/converter/UsdGodotTypeConverter.h) | Undo the spine-axis rotation when authoring the primitive's transform. |
| **Up-axis + meters-per-unit correction applied ONCE at stage root** (Z-up→rotate_x −90°, MPU scale) | `ConvertStagePostProcess` in [UsdGodotStageConverter.h](../../../source/converter/UsdGodotStageConverter.h) | Choose target `upAxis`/`metersPerUnit` and apply the inverse **once at the root**, not per node. Honor `USD_PARENT_UP` / `USD_PARENT_MPU` metadata for nested-stage payloads. |
| **GeomSubset**: only face subsets imported | [MeshConverter.h](../../../shared/include/idtxflow/converter/MeshConverter.h) | Author per-material surfaces back as `GeomSubset`s. |

## Round-trip fidelity via `IUsdNode3D`

Imported nodes carry the metadata needed for faithful round-tripping — read it, don't guess:
[IUsdNode3D.h](../../../shared/include/idtxflow_godot/nodes/IUsdNode3D.h) stores `prim_path`,
`prim_name`, `prim_type`, `stage_path`, and variant-set selections. Use `IUsdNode3D::from_node()`
to detect imported nodes (key export off stored prim path/type). Native Godot nodes never imported
from USD have no such metadata — authoring fresh USD for them is a separate, larger problem; decide
scope explicitly.

## Placement checklist for each new exported element

1. Define/reuse a neutral description struct in `shared/idtxflow/` (no Godot types).
2. Add the inverse leaf conversion `FromTypeConverter::from*` — declared in `shared/idtxflow/`,
   Godot specialization in `idtxflow_godot/`.
3. Extract the description from the scene tree in `source/` (reads `godot::`).
4. Author the USD prim in the agnostic `UsdStageExporter` from the neutral description only.
5. For custom/plugin node types, register an `IPrimExporter<TargetEngineGodot>` (mirror
   `IPrimConverter`, reuse the `IDTXFLOW_GODOT_API` + `extern template` DLL-singleton pattern).
6. Verify manually by round-tripping: import a `.usda`, export it, diff the result.
