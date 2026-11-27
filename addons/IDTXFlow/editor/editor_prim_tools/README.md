# Live primitive tools demo

An experimental editor window for trying primitive actions during a live session.

## Run

1. Create or join a live session with the IDTXFlow addon and open its scene tab.
2. Open `run_prim_tools.gd` in Godot’s Script editor and choose **File > Run**.
3. Select one converted USD prim in the Scene tree.

## Actions

- **Create child:** add an Xform, Cube, Sphere, Cone, Cylinder, Capsule or Plane
  beneath the selected prim. The server assigns its name and default dimensions.
- **Add asset:** browse server files and add a Reference or Payload beneath
  the selected prim.
- **Delete selected:** deactivate the selected prim and its descendants after
  confirmation. Their USD data remains available for restoration.
- **Restore selected:** reactivate the selected inactive prim. Restore an
  inactive ancestor first if necessary.
- **Copy selected:** capture one supported prim's creation spec and show any exclusions.
- **Paste child:** submit that spec beneath the selected parent in one Create request.

Actions become available when the session and selection are eligible.
Server broadcasts update the scene. Closing the window leaves the session running.

## Copy/Paste

Select a prim, click **Copy selected**, select an eligible destination parent,
then click **Paste child**. Each Paste creates a separately named prim.

The GDScript window owns the clipboard and interaction. Copy calls
`UsdStageNode3D.get_prim_create_spec()`; Paste submits the captured spec through
`IdtxClient.request_create_prim()`. Flow provides these APIs for other callers too.

Copy/Paste recreates only the selected prim, including its supported attributes
and local placement. Children are not copied recursively. References and payloads
reuse the same external asset, so its original contents appear through USD
composition; additional children and overrides on descendants are not copied.
