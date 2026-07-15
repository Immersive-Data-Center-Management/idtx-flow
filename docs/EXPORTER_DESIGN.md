# IDTX Flow — USD Exporter Design (Godot → USD)

> **Status:** Design for implementation. **Audience:** an implementing AI/developer.
> Follow this document literally. When a detail is unspecified, prefer the **simplest**
> option and mirror the existing importer rather than inventing new patterns.
>
> **Read first:** the [`usd-exporter` skill](../.github/skills/usd-exporter/SKILL.md) and
> [CLAUDE.md](../CLAUDE.md) sections "Engine-agnostic vs engine-specific" and "Adding an exporter".
> This doc is the concrete, phase-by-phase plan; the skill is the conceptual map.

---

## 0. Locked decisions (do not re-litigate)

| # | Decision | Consequence for implementation |
|---|----------|-------------------------------|
| 1 | Export **both** Godot-native nodes **and** USD-imported nodes. | Node walker must handle nodes with `IUsdNode3D` metadata (imported) **and** plain `Node3D`/`MeshInstance3D`/`Skeleton3D` (native). Prim path/name comes from metadata when present, else synthesized from node name. |
| 2 | **Synchronous** export first, but keep async possible. | Put all scene-tree reading (Godot side) in one place, and all USD authoring (shared side) in a second place that takes only neutral structs. The neutral structs are the thread-boundary-safe hand-off (contain **no** `godot::Object*` pointers) so a future async version can author USD on a worker thread. |
| 3 | Support **`.usda`** (text) **and** a **compressed `.usdc`** (binary) output. | Format is a parameter, not two code paths. USD picks the format from the file extension on `stage->Export(path)` / `UsdStage::CreateNew(path)`. Add a settings/options UI (see §7). |
| 4 | **Always write out textures** next to the stage (self-contained asset). | Texture images are saved to an output folder and referenced by **relative** asset paths in the USD. No `res://` references in exported USD. |

Editor-only for now. Runtime API is designed but not wired to UI.

---

## 1. Architecture overview (mirror the importer exactly)

The importer has two halves separated by a hard boundary. The exporter mirrors it **in reverse**:

```
Godot scene tree ──(read)──►  Neutral export-description structs  ──(author)──►  UsdStage ──► file
   [source/ only]                  [shared, no godot:: types]        [shared, no godot:: types]
```

- **`source/` (Godot-only):** walk `godot::Node3D` subtree, read `ArrayMesh`, `StandardMaterial3D`,
  `Skeleton3D`, and `IUsdNode3D` metadata → fill neutral structs. This is the **only** place
  `godot_cpp/*` is included.
- **`shared/include/idtxflow/` (engine-agnostic):** consume neutral structs, author
  `pxr::UsdGeom*` / `pxr::UsdShade*` prims, write the file. **Never** include `godot_cpp/*` here.

### Importer symbols the exporter inverts (source of truth — read these)

| Concern | Importer (read to invert) |
|---------|---------------------------|
| Stage walk / dispatch | `UsdStageConverter<TargetEngine>` — [StageConverter.h](../shared/include/idtxflow/converter/StageConverter.h) |
| Leaf type conversions | `UsdTypeConverter<TargetEngine>` — [TypeConverter.h](../shared/include/idtxflow/converter/TypeConverter.h) |
| Godot leaf specializations (`toTransform`, `toMaterial`, `toTexture`, `FlipUvV`) | [UsdGodotTypeConverter.h](../shared/include/idtxflow_godot/converter/UsdGodotTypeConverter.h) |
| Mesh shaping | `UsdMeshConverter` / `MeshDescription` — [MeshConverter.h](../shared/include/idtxflow/converter/MeshConverter.h) |
| Godot mesh build / coord+units fix | `ConvertMesh`, `ConvertStagePostProcess` — [UsdGodotStageConverter.h](../source/converter/UsdGodotStageConverter.h) |
| Material channels | `MaterialDescription`, `MaterialChannel` — [MaterialTypes.h](../shared/include/idtxflow/types/MaterialTypes.h) |
| Godot type traits / `MeshData` | [GodotTypes.h](../shared/include/idtxflow_godot/types/GodotTypes.h) |
| Per-node USD metadata | `IUsdNode3D` — [IUsdNode3D.h](../shared/include/idtxflow_godot/nodes/IUsdNode3D.h) |

---

## 2. Files (created in Phase 0 — extend these, do NOT recreate)

New `.cpp` files under `source/**` are compiled automatically (SCons globs `source/*.cpp` and
`source/**/*.cpp` — see [scons/gdextension.py](../scons/gdextension.py) line ~190). Shared code is
header-only. **No SConstruct edits needed** for new headers or `source/*.cpp` files.

All files below **already exist** and compile. Extend them in later phases; the "Extend for"
column says where each phase's new code goes.

| Path | Layer | Status / Extend for |
|------|-------|---------------------|
| [shared/include/idtxflow/exporter/ExportDescription.h](../shared/include/idtxflow/exporter/ExportDescription.h) | agnostic | Neutral structs (§4). Add fields here if a phase needs more neutral data. |
| [shared/include/idtxflow/exporter/FromTypeConverter.h](../shared/include/idtxflow/exporter/FromTypeConverter.h) | agnostic | Inverse leaf-conversion **declarations**. Add `fromVector2` / `fromMaterialChannel` here as needed. |
| [shared/include/idtxflow/exporter/UsdStageExporter.h](../shared/include/idtxflow/exporter/UsdStageExporter.h) | agnostic | Stage authoring. **Phase 2/3/4:** fill `ExportMesh` / add `ExportMaterial` here. |
| [shared/include/idtxflow_godot/exporter/GodotFromTypeConverter.h](../shared/include/idtxflow_godot/exporter/GodotFromTypeConverter.h) | Godot-header | Godot `FromTypeConverter` specializations. Add new `from*` specializations here. |
| [source/exporter/SceneTreeExtractor.h](../source/exporter/SceneTreeExtractor.h) | Godot | Scene-tree walker → neutral structs. **Phase 2/3/4:** read `ArrayMesh` / `StandardMaterial3D` / `Skeleton3D` here. |
| [source/exporter/UsdExporter.h](../source/exporter/UsdExporter.h) / [.cpp](../source/exporter/UsdExporter.cpp) | Godot | Registered class; static `export_node(root, uri, options)` entry (§8). |
| [source/register_types.cpp](../source/register_types.cpp) | Godot | `GDREGISTER_CLASS(UsdExporter)` already added. |
| [addons/IDTXFlow/plugin.gd](../addons/IDTXFlow/plugin.gd) | GDScript | "Export selected node to USD…" Tools-menu item + save dialog. |

> **Not created** (planned in an earlier draft, deliberately omitted): a separate
> `source/exporter/UsdGodotStageExporter.h` — the mesh/material `Export*` methods live in the shared
> `UsdStageExporter.h` because they author `pxr` prims from the neutral structs and need no `godot::`
> specialization. An `export_stage` method on `UsdStageNode3D` was **not** added; the static
> `UsdExporter.export_node` is the single entry point (works for native and imported subtrees).

---

## 3. Coordinate system & units (KEEP IT SIMPLE)

The importer applies a one-time correction at the stage root in `ConvertStagePostProcess`
([UsdGodotStageConverter.h](../source/converter/UsdGodotStageConverter.h) ~line 702): Z-up USD →
`rotate_x(-90°)`, plus `metersPerUnit` scaling.

**Exporter default (Phase 1):** author the stage as **Y-up, metersPerUnit = 1.0** — the same
convention Godot uses. Then **no rotation and no scaling is needed** on any node; Godot local
transforms are written verbatim. Do this:

```cpp
pxr::UsdGeomSetStageUpAxis(stage, pxr::UsdGeomTokens->y);
pxr::UsdGeomSetStageMetersPerUnit(stage, 1.0);
```

Only if a later requirement asks for Z-up output do you apply the inverse of the importer's root
rotation (`rotate_x(+90°)`) once at the exported root prim. **Do not** do per-node axis math.

---

## 4. Neutral export-description structs

These are the hand-off contract and are **already implemented** in
[ExportDescription.h](../shared/include/idtxflow/exporter/ExportDescription.h). They use `pxr`
math/geometry types (allowed in the agnostic layer) but **never** `godot::` types and **never** raw
pointers to engine objects. The actual shapes (read the file for the authoritative version):

```cpp
namespace idtxflow::exporter {

// A texture referenced by a material channel, captured as raw bytes for self-contained export.
struct TextureExportRef {                  // top-level (NOT nested, NOT templated)
    std::string sourceImagePath;           // original on-disk path if known (may be empty)
    std::vector<uint8_t> imageBytes;        // encoded image bytes (e.g. PNG) to write out
    std::string extension = ".png";
    pxr::GfVec2f uvScale  = {1, 1};
    pxr::GfVec2f uvOffset = {0, 0};
};

// One material resolved to neutral channel values. NON-templated.
struct MaterialExportDescription {
    std::string name;                       // sanitized material prim name
    std::map<types::MaterialChannel, pxr::GfVec4f> scalarChannels;   // colours full rgba; scalars in .x
    std::map<types::MaterialChannel, TextureExportRef> textureChannels;
    float opacityThreshold = 0.0f;
};

// One node in the exported hierarchy. Templated on TargetEngine (for Types::MeshData).
template<typename TargetEngine> requires idtxflow::types::ValidTargetEngine<TargetEngine>
struct NodeExportDescription {
    using Types = idtxflow::types::TargetEngineTypes<TargetEngine>;
    enum class Kind { Xform, Mesh, Skeleton };

    std::string primName;                   // sanitized, valid USD identifier
    std::optional<std::string> originalPrimPath;  // from IUsdNode3D if imported; else nullopt
    std::optional<std::string> originalPrimType;  // from IUsdNode3D if imported; else nullopt

    pxr::GfMatrix4d localTransform = pxr::GfMatrix4d(1.0);  // node-local transform (identity default)
    Kind kind = Kind::Xform;

    // Only for Kind::Mesh. One MeshData per surface; materials index-aligned to surfaces.
    std::vector<typename Types::MeshData> surfaces;
    std::vector<std::optional<MaterialExportDescription>> surfaceMaterials;

    std::vector<NodeExportDescription<TargetEngine>> children;
};

struct StageExportOptions {
    std::string outputStagePath;            // absolute; extension decides format (.usda/.usdc/.usdz)
    std::string textureOutputDir;           // absolute dir where textures are written
    bool upAxisY = true;                    // true=Y-up MPU=1 (default); false=Z-up (later)
};

} // namespace
```

> **Note on `MeshData`:** it lives in [GodotTypes.h](../shared/include/idtxflow_godot/types/GodotTypes.h)
> and *does* contain `godot::Packed*Array`. That is acceptable at the hand-off because those are
> value types (copyable, no scene-graph pointers). The **agnostic exporter must not call any
> `godot::` method on them** other than element access; treat them as plain arrays. If strict
> purity is desired later, add a neutral `MeshDataNeutral` (std::vector based) — **not required for v1.**

---

## 5. Godot-side extraction (`source/exporter/`)

`SceneTreeExtractor` walks the subtree and builds `NodeExportDescription`. Rules:

### 5.1 Node classification
For each `godot::Node3D`:
1. `IUsdNode3D* iu = IUsdNode3D::from_node(node);` (use this, **never** `dynamic_cast`).
   If non-null → fill `originalPrimPath = iu->get_prim_path()`, `originalPrimType = iu->get_prim_type()`,
   `primName = iu->get_prim_name()`.
2. Determine `Kind`:
   - `Object::cast_to<godot::MeshInstance3D>(node)` non-null (covers `UsdMeshInstanceNode3D`) → `Kind::Mesh`.
   - `Object::cast_to<godot::Skeleton3D>(node)` non-null (covers `UsdSkeletonNode3D`) → `Kind::Skeleton` (Phase 4+).
   - else → `Kind::Xform`.
3. `primName`: if from metadata use it; else use `node->get_name()`. **Gotcha:** `get_name()`
   returns `godot::StringName` — assign it to a `godot::String` first (a ternary mixing `StringName`
   and `String` is ambiguous and won't compile). The name is turned into a valid USD identifier by
   `SanitizePrimName()` (called **in the extractor** immediately on assignment — `UsdStageExporter.h`
   is included for this). ✅ **Done (Phase 1):** sibling-name uniqueness is enforced — before pushing
   each child to `out.children`, a loop checks existing siblings and appends `_1`, `_2`, … on
   collision.

### 5.2 Transform
`localTransform = FromTypeConverter<Godot>::fromTransform(node->get_transform());`
`get_transform()` returns the **local** transform (relative to parent) — exactly what USD
`xformOp:transform` expects. Do **not** use `get_global_transform()`.

### 5.3 Mesh extraction (Phase 2)
For a `MeshInstance3D`, get `Ref<ArrayMesh> mesh = node->get_mesh()`. If it is a primitive mesh
(`BoxMesh` etc.) you may still read it via `get_mesh_arrays`/surface arrays. For each surface
`i` in `0..mesh->get_surface_count()-1`:

```cpp
godot::Array a = mesh->surface_get_arrays(i);
MeshData md;
md.Vertices  = a[godot::Mesh::ARRAY_VERTEX];   // PackedVector3Array
md.Normals   = a[godot::Mesh::ARRAY_NORMAL];   // may be empty
md.UVs       = a[godot::Mesh::ARRAY_TEX_UV];   // may be empty
md.VertexColors = a[godot::Mesh::ARRAY_COLOR]; // may be empty
md.Triangles = a[godot::Mesh::ARRAY_INDEX];    // PackedInt32Array, triangle list
// bones/weights: a[ARRAY_BONES] / a[ARRAY_WEIGHTS] (Phase 4)
```

> **Gotcha:** `godot::Array::operator[]` returns a `Variant`. Depending on compiler settings the
> implicit `Variant → Packed*Array` assignment may be ambiguous — if it doesn't compile, wrap it
> explicitly, e.g. `md.Vertices = godot::PackedVector3Array(a[godot::Mesh::ARRAY_VERTEX]);`
> (same pattern used for `Dictionary::get` in `UsdExporter.cpp`).

These are the **same arrays** the importer built in `ConvertMesh`
([UsdGodotStageConverter.h](../source/converter/UsdGodotStageConverter.h) ~line 395) — just read back.
Material per surface: `mesh->surface_get_material(i)` (fall back to
`node->get_surface_override_material(i)` if set). Cast to `StandardMaterial3D` (§5.4).

### 5.4 Material extraction (Phase 3)
From `Ref<StandardMaterial3D> m`:
| USD/neutral channel | Godot getter |
|---|---|
| Diffuse (color) | `m->get_albedo()` → `GfVec4f(r,g,b,a)` |
| Diffuse (texture) | `m->get_texture(BaseMaterial3D::TEXTURE_ALBEDO)` |
| Metallic | `m->get_metallic()` (+ `TEXTURE_METALLIC`) |
| Roughness | `m->get_roughness()` (+ `TEXTURE_ROUGHNESS`) |
| Emissive | `m->get_emission()` (+ `TEXTURE_EMISSION`) |
| Normal | `m->get_texture(TEXTURE_NORMAL)` |
| AO | `m->get_texture(TEXTURE_AMBIENT_OCCLUSION)` |
| UV scale/offset | `m->get_uv1_scale()`, `m->get_uv1_offset()` |

For each texture: get the `Ref<Texture2D>`, call `tex->get_image()`, then `image->save_png_to_buffer()`
(or `save_png` to the output dir). Store bytes/extension in `TextureRef`. **Un-flip nothing here**;
V-flip inversion happens in the shared mesh authoring (§6.3).

---

## 6. Shared-side authoring (`shared/include/idtxflow/exporter/`)

`UsdStageExporter<TargetEngine>` mirrors `UsdStageConverter`. It is **already implemented** in
[UsdStageExporter.h](../shared/include/idtxflow/exporter/UsdStageExporter.h) with these members
(extend `ExportMesh` and add `ExportMaterial` in later phases):

```cpp
template<typename TargetEngine> requires idtxflow::types::ValidTargetEngine<TargetEngine>
class UsdStageExporter {
public:
    using Types = idtxflow::types::TargetEngineTypes<TargetEngine>;
    using NodeDesc = exporter::NodeExportDescription<TargetEngine>;
    // Author a full stage from a root description and write it to options.outputStagePath.
    bool Export(const NodeDesc& root, const exporter::StageExportOptions& options);
protected:
    // Recurse: author this node's prim by kind, then its children. Returns the created prim.
    pxr::UsdPrim ExportNode (const pxr::UsdStageRefPtr&, const pxr::SdfPath& parent, const NodeDesc&, const exporter::StageExportOptions&);
    pxr::UsdPrim ExportXform(const pxr::UsdStageRefPtr&, const pxr::SdfPath&, const NodeDesc&);
    pxr::UsdPrim ExportMesh (const pxr::UsdStageRefPtr&, const pxr::SdfPath&, const NodeDesc&);
    // Phase 3 (to add): author a UsdShadeMaterial from a (non-templated) MaterialExportDescription
    //   pxr::UsdPrim ExportMaterial(stage, matPath, const exporter::MaterialExportDescription&, const StageExportOptions&);
};
```

### 6.1 Stage creation & recursion (implemented)
```cpp
// Author in-memory, then Export() to the path. Export OVERWRITES an existing file and picks the
// format (.usda/.usdc/.usdz) from the extension. CreateNew would FAIL if the file already exists
// (a save dialog routinely targets existing files) — so do NOT use CreateNew here.
auto stage = pxr::UsdStage::CreateInMemory();
pxr::UsdGeomSetStageUpAxis(stage, options.upAxisY ? pxr::UsdGeomTokens->y : pxr::UsdGeomTokens->z);
pxr::UsdGeomSetStageMetersPerUnit(stage, 1.0);
pxr::UsdPrim rootPrim = ExportNode(stage, pxr::SdfPath::AbsoluteRootPath(), root, options);
if (rootPrim) stage->SetDefaultPrim(rootPrim);
stage->GetRootLayer()->Export(options.outputStagePath);
```

Path building: `parentPath.AppendChild(pxr::TfToken(desc.primName))`. If `desc.originalPrimPath`
is present and decision-2 round-trip fidelity is wanted, you *may* honor it — but for v1 **derive
paths from the hierarchy** to guarantee validity; keep `originalPrimType` only to choose the prim
schema for imported prims (e.g. re-emit a `Sphere` as `UsdGeomSphere` instead of a mesh — optional,
Phase 5).

### 6.2 Xform authoring (Phase 1)
```cpp
auto xform = pxr::UsdGeomXform::Define(stage, path);
xform.AddTransformOp().Set(desc.localTransform);   // GfMatrix4d, single matrix op
```

### 6.3 Mesh authoring (Phase 2)
For a `Kind::Mesh` node, define **one `UsdGeomMesh`** and split surfaces into `GeomSubset`s
(inverse of import's subset splitting), OR (simpler v1) define one mesh per surface as children.
**Choose one-mesh-with-subsets** to match import symmetry. Per surface's `MeshData md`:
- Accumulate `points` (from `md.Vertices`), `faceVertexIndices` (from `md.Triangles`),
  `faceVertexCounts` = all `3` (triangles). Offset indices when concatenating surfaces.
- Normals: author `normals` primvar with interpolation `faceVarying` or `vertex` matching how
  vertices were emitted (import emits per-corner; use `faceVarying`). Set via
  `mesh.CreateNormalsAttr()` + `mesh.SetNormalsInterpolation(...)`.
- UVs: author `primvars:st` (`TfToken("st")`) as `faceVarying` `TexCoord2fArray`. **Invert the
  import V-flip**: write `st.y = -godot_uv.y` (import did `uv.y = -uv.y`; see `FlipUvV` in
  [UsdGodotTypeConverter.h](../shared/include/idtxflow_godot/converter/UsdGodotTypeConverter.h)).
- Vertex colors: optional `primvars:displayColor`/`displayOpacity`.
- Per surface, create a `UsdGeomSubset` (elementType `face`) listing that surface's face indices,
  and bind its material (§6.4).

### 6.4 Material authoring (Phase 3)
```cpp
auto mat = pxr::UsdShadeMaterial::Define(stage, matPath);
auto shader = pxr::UsdShadeShader::Define(stage, matPath.AppendChild(TfToken("Preview")));
shader.CreateIdAttr(pxr::VtValue(pxr::TfToken("UsdPreviewSurface")));
// diffuseColor, metallic, roughness, emissiveColor, opacity, normal
shader.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f).Set(GfVec3f(...));
// connect surface output; bind material to the mesh/subset via UsdShadeMaterialBindingAPI
```
Textures: copy each `TextureRef` image into `options.textureOutputDir`, author a
`UsdUVTexture` shader with `inputs:file` = **relative** asset path, `st` reader
(`UsdPrimvarReader_float2`, varname `st`), apply `uvScale`/`uvOffset` via a transform2d node or the
reader's scale/translation. Map channels using the inverse of `toMaterial`
([UsdGodotTypeConverter.h](../shared/include/idtxflow_godot/converter/UsdGodotTypeConverter.h)
~line 156). **Remember Godot has one UV set** — write a single `st` primvar; do not fabricate
multiple UV sets.

### 6.5 `FromTypeConverter` (inverse leaf conversions)
Declared in [FromTypeConverter.h](../shared/include/idtxflow/exporter/FromTypeConverter.h),
specialized for Godot in
[GodotFromTypeConverter.h](../shared/include/idtxflow_godot/exporter/GodotFromTypeConverter.h).
`fromTransform`, `fromVector3`, `fromColor` are **already implemented**; add more `from*` here as
phases need them:
```cpp
template<> pxr::GfMatrix4d FromTypeConverter<TargetEngineGodot>::fromTransform(const godot::Transform3D& t);
template<> pxr::GfVec3f    FromTypeConverter<TargetEngineGodot>::fromVector3(const godot::Vector3& v);
template<> pxr::GfVec4f    FromTypeConverter<TargetEngineGodot>::fromColor(const godot::Color& c);
```
`fromTransform` must be the exact inverse of `toTransform`
([UsdGodotTypeConverter.h](../shared/include/idtxflow_godot/converter/UsdGodotTypeConverter.h) ~line 75).
Concretely, build the USD row-major matrix from the Godot basis **columns** and origin:
```cpp
godot::Basis b = t.basis; godot::Vector3 o = t.origin;
godot::Vector3 c0 = b.get_column(0), c1 = b.get_column(1), c2 = b.get_column(2);
pxr::GfMatrix4d m;
m.SetRow(0, pxr::GfVec4d(c0.x, c0.y, c0.z, 0));
m.SetRow(1, pxr::GfVec4d(c1.x, c1.y, c1.z, 0));
m.SetRow(2, pxr::GfVec4d(c2.x, c2.y, c2.z, 0));
m.SetRow(3, pxr::GfVec4d(o.x,  o.y,  o.z,  1));
return m;
```
**Validation is mandatory:** round-trip a known transform through import `toTransform` then export
`fromTransform` and assert equality (see §9). If it mismatches, transpose the basis mapping — the
Godot↔USD basis/row convention is the #1 source of bugs.

---

## 7. Editor UI ([addons/IDTXFlow/plugin.gd](../addons/IDTXFlow/plugin.gd))

**Already implemented.** `plugin.gd` registers a "Tools" menu item "Export selected node to USD…"
that: resolves the export root (first selected `Node3D`, else `get_edited_scene_root()`), opens an
`EditorFileDialog` (`FILE_MODE_SAVE_FILE`, `ACCESS_FILESYSTEM` so it yields absolute OS paths) with
`*.usda / *.usdc / *.usdz` filters, and on confirm calls
`UsdExporter.export_node(root, path, { "texture_dir": path.get_base_dir(), "up_axis_y": true })`.

Extend later: an optional second menu item "Export scene to USD…" (root = scene root) and/or a
settings dialog (`export_dialog.tscn`) for texture folder / options — only if more options appear
(decision 3/4). `ACCESS_FILESYSTEM` yields absolute paths; exporting to `res://`/`user://` needs the
resolver (see §13 open questions).

---

## 8. C++ binding entry point

**(a) Implemented — static utility, works for any root incl. native Godot scenes.**
[UsdExporter](../source/exporter/UsdExporter.h) is a `godot::Object` registered with
`GDREGISTER_CLASS`, exposing a **static** method bound via
`ClassDB::bind_static_method("UsdExporter", D_METHOD("export_node", "root", "uri", "options"), ...)`
so GDScript calls `UsdExporter.export_node(root, uri, options)` without instantiation:
```cpp
// options keys: "texture_dir": String, "up_axis_y": bool. Returns success.
static bool export_node(godot::Node3D* root, const godot::String& uri, const godot::Dictionary& options);
```
Body (in [UsdExporter.cpp](../source/exporter/UsdExporter.cpp)): fills `StageExportOptions`
(cast `Dictionary::get(...)` results explicitly — `bool(...)`, `String(...)`), runs
`SceneTreeExtractor::Extract(root)`, then `UsdStageExporter<TargetEngineGodot>().Export(...)`.

**(b) Optional future — on `UsdStageNode3D`:** a `bool export_stage(uri, options)` that exports its
own subtree (it already owns the resolver/cache). Bind in `_bind_methods`
([UsdStageNode3D.cpp](../source/nodes/UsdStageNode3D.cpp) ~line 381) like `set_stage_uri`. Not needed
while (a) covers all roots; add it if the resolver wiring becomes necessary for `res://` output.

The exporter is **synchronous** (decision 2). Because extraction and authoring are split by the
neutral structs, a later async version only needs to run authoring + file write on a worker thread
while extraction stays on the main thread — do **not** touch `godot::` objects off the main thread.

---

## 9. Verification (no automated test suite exists)

Manual round-trip is the acceptance test for every phase:
1. Build (`scons platform=windows target=template_debug` — see [AGENTS.md](../AGENTS.md)).
2. Copy `addon/IDTXFlow/` into a Godot test project's `addons/`, enable the plugin.
3. Import a known `.usda` via `UsdStageNode3D.stage_uri`.
4. Select the stage node → "Export selected node to USD…" → save `.usda`.
5. Open the exported `.usda` in a text editor and diff against the original; re-import the exported
   file into a second `UsdStageNode3D` and visually compare.

**Transform sanity check (Phase 1 — verified statically):** `toTransform` passes USD matrix
row `i` as the `i`-th column vector to the `godot::Basis` constructor. `fromTransform` reads
`Basis::column(i)` and writes it as USD row `i`. These are exact inverses (modulo float↔double
rounding, which is one-way and acceptable). The spine-axis rotation in `toTransform` only applies
to `Cone`/`Cylinder` prims and is irrelevant for `Xform`/`Mesh` export. A live round-trip check
(import → export → re-import → compare `global_transform`) is still recommended before shipping
but is not expected to reveal a bug in the basis mapping.

---

## 10. Phased delivery (implement in order; each phase ends with a working round-trip)

| Phase | Scope | Done when |
|-------|-------|-----------|
| **0. Plumbing** | ~~Create the files in §2 as stubs. `UsdStageExporter::Export` writes a `.usda` with one root `Xform`, Y-up, MPU=1. Wire the editor button + file dialog + `export_node` binding.~~ **✅ DONE** (compiles, links, installed). Produces a valid USD file with the full node hierarchy + transforms; mesh/material authoring stubbed. | Clicking the button produces a valid, openable USD file with the node hierarchy. |
| **1. Transforms** | ~~Mechanics already exist (walker recurses, `fromTransform` implemented, `ExportXform` authors nested `UsdGeomXform`). **Remaining:** (a) **validate** the `fromTransform` round-trip (§9) and fix the basis/row convention if needed; (b) add sibling-name de-dup in `SceneTreeExtractor` (§5.1).~~ **✅ DONE.** Transform math verified correct (USD row `i` ↔ Basis column `i`). Sanitization applied at name-assignment time; sibling de-dup (`_1`, `_2`, …) added before `out.children.push_back`. | A transform-only hierarchy round-trips; `global_transform` matches within epsilon; no prim-name collisions. |
| **2. Meshes** | ~~Read `ArrayMesh` surfaces (§5.3). `ExportMesh` authors one `UsdGeomMesh` with `points`/`faceVertexCounts=3`/`faceVertexIndices`, `normals`, `primvars:st` (V un-flipped), per-surface `GeomSubset`.~~ **✅ DONE.** `SceneTreeExtractor` reads all surface arrays; `ExportMesh` accumulates across surfaces, authors `points`/`faceVertexIndices`/`faceVertexCounts`/`normals` (vertex interp), `primvars:st` (V un-flipped, vertex interp), `primvars:displayColor`. `GeomSubset`s created for multi-surface meshes (material binding added in Phase 3). | A mesh (multi-surface) round-trips with correct geometry, normals, and UVs (no V flip). |
| **3. Materials + textures** | ~~Extract `StandardMaterial3D` (§5.4). `ExportMaterial` authors `UsdPreviewSurface` + `UsdUVTexture`; **always writes texture files** to `texture_dir` with relative refs; bind to mesh/subset.~~ **✅ DONE.** `SceneTreeExtractor` reads albedo/metallic/roughness/specular/emissive/normal/AO texture slots + scalar values; image bytes captured via `Image::save_png_to_buffer()`. `ExportMaterial` authors `UsdPreviewSurface` + shared `UsdPrimvarReader_float2(st)` + per-channel `UsdUVTexture`; writes PNG files to `textureOutputDir`; connects outputs to shader inputs; binds via `UsdShadeMaterialBindingAPI` to mesh (single surface) or `UsdGeomSubset` (multi-surface). | Exported `.usda` + texture files open standalone; materials/textures visible on re-import. |
| **4. Skeletons & skinning** | Read `Skeleton3D` + `ARRAY_BONES`/`ARRAY_WEIGHTS`; author `UsdSkel`. Mirror `SkeletonConverter`. Accept the ≤4-influence limit (lossy, one-way). | A skinned mesh round-trips and deforms. |
| **5. Extras** | Animation (`UsdSkelAnimation`), colliders (`IDTXCollisionAPI`), re-emit imported primitive prims (`Sphere`/`Cube`) using `originalPrimType`, and an `IPrimExporter<TargetEngineGodot>` registry mirroring `IPrimConverter` (reuse the `IDTXFLOW_GODOT_API` + `extern template` DLL-singleton pattern). | Custom node types export via registry without modifying core. |

---

## 11. Lossy inversions cheat-sheet (apply on the correct side)

| Import decision (file) | Export action | Where in exporter |
|---|---|---|
| UV `v = -v` (`FlipUvV`, [UsdGodotTypeConverter.h](../shared/include/idtxflow_godot/converter/UsdGodotTypeConverter.h)) | write `st.y = -godot_uv.y` | §6.3 (shared authoring) |
| Bone weights clamped to 4/8, renormalized ([UsdGodotTypeConverter.h](../shared/include/idtxflow_godot/converter/UsdGodotTypeConverter.h)) | export clamped weights as-is (unrecoverable) | Phase 4 |
| n-gon fan triangulation ([MeshConverter.h](../shared/include/idtxflow/converter/MeshConverter.h)) | export triangles (`faceVertexCounts` all 3) | §6.3 |
| up-axis + MPU root correction ([UsdGodotStageConverter.h](../source/converter/UsdGodotStageConverter.h)) | author Y-up, MPU=1 → identity (default) | §3, §6.1 |
| spine-axis rotation for Cone/Cylinder (`toTransform`) | only relevant if re-emitting native primitive prims (Phase 5); else meshes bake it already | Phase 5 |
| one Godot UV set / one uv scale-offset | write single `st`; don't fabricate USD UV sets | §6.4 |

---

## 12. Hard rules (repeat)

1. **Never** include `godot_cpp/*` in `shared/include/idtxflow/` (agnostic layer). Compilation
   enforces the boundary — if you're tempted, you're on the wrong side.
2. Scene-tree reading only in `source/` / `idtxflow_godot/`; USD authoring only in shared exporter.
3. Use `IUsdNode3D::from_node()`, never `dynamic_cast`, to detect imported nodes.
4. Do not read `godot::` objects off the main thread (async-readiness).
5. Verify each phase by round-trip before starting the next.

---

## 13. Scaffold status & open questions (Phase 0 done)

**Implemented (Phase 0 — compiles & links, Windows debug):** the full call path exists and produces
a real USD file with the node hierarchy + transforms.

| File | Role |
|------|------|
| `shared/include/idtxflow/exporter/ExportDescription.h` | neutral structs |
| `shared/include/idtxflow/exporter/FromTypeConverter.h` | inverse leaf conversion declarations |
| `shared/include/idtxflow/exporter/UsdStageExporter.h` | agnostic stage authoring (Xform real; Mesh = empty `UsdGeomMesh`; Skeleton = Xform placeholder) |
| `shared/include/idtxflow_godot/exporter/GodotFromTypeConverter.h` | Godot `fromTransform/fromVector3/fromColor` |
| `source/exporter/SceneTreeExtractor.h` | walks the Godot tree → neutral structs (transforms + kind; surfaces empty) |
| `source/exporter/UsdExporter.{h,cpp}` | Godot-registered class; `UsdExporter.export_node(root, uri, options)` |
| `source/register_types.cpp` | `GDREGISTER_CLASS(UsdExporter)` |
| `addons/IDTXFlow/plugin.gd` | "Export selected node to USD…" Tools-menu item + save dialog |

**Better approaches adopted during implementation (reflected above):**
- **Overwrite handling:** author into `UsdStage::CreateInMemory()` then `GetRootLayer()->Export(path)`.
  `Export` overwrites existing files and selects `.usda/.usdc/.usdz` by extension; `CreateNew`
  **fails if the file exists** (a save dialog routinely targets existing files). Use the in-memory +
  Export pattern in all phases.

**Gotchas found (already fixed — keep in mind when extending):**
- `godot::Node::get_name()` returns `StringName`, not `String`; converting via a ternary with a
  `String` is ambiguous — assign to a `String` first.
- `godot::Dictionary::get(...)` returns `Variant`; cast explicitly (`bool(...)`, `String(...)`)
  before assigning to typed fields.

**Open questions to resolve in later phases:**
1. **`res://` / `user://` output paths.** The editor dialog uses `ACCESS_FILESYSTEM` → absolute OS
   paths, which USD writes directly on desktop. Exporting to `res://`/`user://` (needed at runtime,
   or for in-project exports) must route through the Godot asset resolver like the importer does.
   Deferred.
2. **Mesh data crosses the boundary as `godot::Packed*Array`** (via `Types::MeshData` in
   `NodeExportDescription::surfaces`). Phase 2's mesh authoring in the *shared* layer will read those
   arrays element-wise — accepted per §4. If strict purity becomes necessary, add a std::vector-based
   neutral mesh struct and convert on the Godot side. **Decide before starting Phase 2.**
3. **`add_tool_menu_item` callback arity** — assumed to be called with no arguments (Godot 4.5).
   Verify in-editor; if it passes a `Variant`, adjust `_on_export_pressed`.
4. **Material/skeleton kinds** currently fall back to `UsdGeomMesh`/`UsdGeomXform` placeholders;
   replace with real authoring in phases 2–4.

