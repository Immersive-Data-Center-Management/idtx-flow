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

Actions become available when the session and selection are eligible.
Server broadcasts update the scene. Closing the window leaves the session running.
