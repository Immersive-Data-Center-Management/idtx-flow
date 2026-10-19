# Transform Sync — Inbound & Outbound Flows

How a transform edit travels between the Godot editor and collaborating peers in
the IDTXFlow real-time client.

---

The layers this builds on — the engine-agnostic core, its ports & adapters, the
threading model, and what a new host engine reuses vs. implements — are described in
[net-core.md](net-core.md) and [godot-binding.md](godot-binding.md). This document
focuses on how a single transform edit travels **outbound** (a local gizmo drag ->
peers) and **inbound** (a peer edit -> my stage + UI). How an edit is *authored*
onto the stage (the node-owned bridge, the local-or-session gate, placement-root
and nested-stage handling) is in [stage-authoring.md](stage-authoring.md).

---


## Outbound — local gizmo drag to broadcast

```mermaid
sequenceDiagram
    autonumber
    participant Node as UsdXformNode3D
    participant StageNode as UsdStageNode3D
    participant Ctl as StageEditController
    participant Bridge as StageBridge
    participant Stage as USD Stage
    participant Engine as CollabEngine
    participant Socket as SessionSocket
    participant Wire as Wire / WS

    Note over Node: NOTIFICATION_TRANSFORM_CHANGED (gizmo drag)
    Node->>StageNode: author_node_transform(this)
    StageNode->>Ctl: author_from_node(child, local_authoring, author_root)
    Note over Ctl: gate - session sink installed OR local authoring, skip placement root
    Ctl->>Bridge: author_local_edit(transform_to_prim_edit(xform))
    Bridge->>Stage: author_to_usd, set xform op (free local save)
    Stage-->>Bridge: TfNotice _on_objects_changed (de-duped per prim)
    Bridge-->>Engine: on_changed(read_prim yields PrimEdit)
    Note over Engine: gate remote AND armed AND snapshot_complete AND socket open AND NOT applying_remote
    Engine->>Socket: send(edit) [immediate]
    Note over Socket: drop if identical to last sent for this prim
    Socket->>Wire: encode_transform_update then send_binary
```

**Key points**
- **Author into USD first.** The stage node routes the edit through
  `StageEditController` to `author_local_edit`, which writes the live stage — that
  write *is* the free local save. The USD `TfNotice` is the single "stage changed"
  source, so editor gizmo edits and script edits broadcast through the exact same
  gate. The authoring decision (local vs. session, placement-root/sub-stage rules)
  lives in [stage-authoring.md](stage-authoring.md).
- **One path, local or session.** With no session the same author runs but no sink
  is installed, so nothing broadcasts (local-only save). A session installs the
  sink via `attach_stage`, and the identical author now also feeds the broadcast.
- **The broadcast gate** (CollabEngine::on_stage_changed) sends only when
  remote_ AND armed_ AND snapshot_complete_ AND the socket is open AND NOT
  applying_remote_; otherwise the edit is dropped.
- **Arming (two gates).** A remote session arms only when **both** hold: the
  post-attach settle elapsed (`arm_countdown_` = kArmAfterTicks, 3 frames, so USD
  load-time writes settle and do not phantom-broadcast) **and** the join snapshot
  completed (`SnapshotComplete`) — the protocol forbids sending updates before the
  snapshot. A reconnect re-gates on the snapshot only (`snapshot_complete_` is
  cleared and the session disarmed); the settle latch stays set, since a reconnect
  does not reconvert the stage.
- **Immediate send + duplicate suppression.** The bridge de-dupes per prim within a
  notice; `SessionSocket::send` additionally drops an edit identical (payload only,
  ignoring the timestamp) to the last one it sent for that prim. There is no
  per-frame flush — the edit goes out the moment it is authored (Godot already
  delivers one transform notice per node per frame).
- **Ordering & acks.** Each outbound frame carries the latest applied `server_seq`
  as its base, captured at send time (the server rejects a stale base), and a client
  `request_id` echoed in the matching Ack. Acks are **diagnostic only** — the client
  adopts the ack's `server_seq` (so its own applied edit advances its base) and does
  not resend; the server resolves ordering. An edit authored while disconnected is
  dropped, and the join snapshot is authoritative on reconnect. See
  [net-core.md](net-core.md).

---

## Inbound — peer edit to my stage + UI

```mermaid
sequenceDiagram
    autonumber
    participant Wire as Wire / WS
    participant Socket as SessionSocket
    participant Engine as CollabEngine
    participant Bridge as StageBridge
    participant Stage as USD Stage
    participant GNode as Godot Node3D
    participant Client as IdtxClient
    participant UI as GDScript / UI

    Wire->>Socket: on_binary (network thread)
    Socket->>Engine: decode, RemoteEdit, on_remote_edit
    Note over Engine: dispatcher.post to main thread, applying_remote = true
    Engine->>Bridge: apply_remote_edit(edit)
    Note over Bridge: suppress_broadcast = true (kills TfNotice echo)
    Bridge->>Stage: author_to_usd(prim_edit_to_transform)
    Stage-->>Bridge: TfNotice (suppressed, no re-broadcast)
    Bridge->>GNode: set_transform (block_signals)
    Bridge-->>Engine: applied, applying_remote = false
    Engine->>Client: on_remote_edit(edit, from)
    Client->>UI: emit transform_broadcast_received
```

**Key points**
- **Thread hop.** Socket callbacks arrive on the transport network thread;
  CollabEngine::open_session_socket's on_remote_edit lambda does
  ports_.dispatcher->post(...) so the apply + observer notification run on the
  Godot main thread.
- **Triple echo prevention** (all three are needed):
  1. applying_remote_ (engine) — on_stage_changed refuses to broadcast while
     a remote edit is being applied.
  2. suppress_broadcast_ (bridge) — the author_to_usd trips a TfNotice, but
     the listener is suppressed so it does not re-send the peer edit.
  3. set_block_signals(true/false) around set_transform — moving the node
     does not fire its NOTIFICATION_TRANSFORM_CHANGED (the outbound path).
- **Stage stays authoritative.** The edit is authored into USD, then the Godot
  Node3D is set to match. The transform_broadcast_received signal is
  informational for UI only — the stage and node already moved in
  apply_remote_edit.

---

## Detailed step-by-step (annotated)

### OUTBOUND — a local gizmo drag → broadcast to peers

```text
User drags gizmo on a UsdXformNode3D (or UsdMeshInstanceNode3D)
        │  Godot fires NOTIFICATION_TRANSFORM_CHANGED
        ▼
[1] source/nodes/UsdXFormNode3D.cpp :: _notification()          (adapter — node)
        │   stage node (nearest UsdStageNode3D) ->author_node_transform(this)
        ▼
[2] source/nodes/UsdStageNode3D.cpp :: author_node_transform()   (adapter)
        │   • StageEditController(get_or_create_bridge()).author_from_node(child, local_authoring_, author_placement_root_)
        │   • gate: skip unless a session sink is installed OR local_authoring is on; skip the placement root
        │   • child->get_transform()  → PrimEdit via
        │     gxform::transform_to_prim_edit()   ── TransformCodec ──▶ ConventionMath::wire_from_basis_origin
        ▼
[3] source/stage_ops/StageEditController.cpp :: author_from_node()   (adapter)
        │   bridge_->author_local_edit(edit)          ← writes the LOCAL SAVE first
        ▼
[4] source/stage_ops/StageBridge.cpp :: author_local_edit()   (adapter, Godot+pxr)
        │   (not suppressed) → author_to_usd(prim_path,
        │        gxform::prim_edit_to_transform(edit))
        │        └─ transform_to_gfmatrix → ConventionMath::usd_from_wire → pxr::GfMatrix4d
        │   Sets the USD xform op on the live stage  →  trips a USD TfNotice
        ▼
[5] StageBridge :: _on_objects_changed()  (TfNotice listener)
        │   suppress_broadcast_? no.  For a tracked prim → read_prim()
        │        └─ read_prim_transform + gxform::transform_to_prim_edit → PrimEdit
        │   on_changed_(edit)   ← the sink CollabEngine installed in attach_stage
        ▼
[6] CollabEngine.cpp :: on_stage_changed()      (THE GATE)
        │   send only if  remote_ && armed_ && snapshot_complete_ && socket open && !applying_remote_
        │   socket_->send(edit)                 ← otherwise the edit is dropped
        ▼
[7] shared/.../SessionSocket.cpp :: send()   (agnostic)
        │   drop if identical (payload only) to the last edit sent for this prim
        │   stamp timestamp (IClock::now_millis), take a request_id, then:
        │   ws_->send_binary( wire::encode_transform_update(session_id, edit, base_server_seq, request_id) )
        ▼
[8] shared/.../wire/WireCodec.cpp :: encode_transform_update()   (protobuf, agnostic)
        ▼
[9] IWebSocketTransport::send_binary → IxWebSocketTransport → server → peers
```

### INBOUND — a peer edit → applied to my stage + surfaced to UI

```text
Server pushes a binary frame on the WebSocket
        ▼
[1] IxWebSocketTransport (network thread) → set_on_binary
        ▼
[2] shared/.../SessionSocket.cpp :: handle_binary()          (agnostic)
        │   wire::decode(bytes) → DecodedMessage
        │   Kind::RemoteEdit → on_remote_edit_(edit, from_client_id)
        ▼
[3] shared/.../wire/WireCodec.cpp :: decode()                    (protobuf, agnostic)
        ▼
[4] CollabEngine.cpp :: open_session_socket() lambda  (socket_->on_remote_edit)
        │   ports_.dispatcher->post(...)   ← HOP TO MAIN THREAD (Dispatcher)
        │     on main thread:
        │       applying_remote_ = true;               ← LOOPBACK SUPPRESSION (engine side)
        │       ports_.stage->apply_remote_edit(edit);
        │       applying_remote_ = false;
        │       observer_->on_remote_edit(edit, from)   ← notify UI
        ▼
[5a] source/stage_ops/StageBridge.cpp :: apply_remote_edit()   (adapter, Godot+pxr)
        │   suppress_broadcast_ = true;                ← LOOPBACK SUPPRESSION (bridge/TfNotice side)
        │   xform = gxform::prim_edit_to_transform(edit)   ── TransformCodec
        │   author_to_usd(prim_path, xform)            ← write into USD (keeps stage authoritative)
        │   resolve ObjectID → live Node3D:
        │       node->set_block_signals(true);
        │       node->set_transform(xform);            ← move the actual Godot node
        │       node->set_block_signals(false);
        │   suppress_broadcast_ = false;
        │
        │   (the author_to_usd here fires a TfNotice → _on_objects_changed,
        │    but suppress_broadcast_==true so it does NOT re-broadcast — the echo is killed)
        ▼
[5b] source/collab/IdtxClient.cpp :: on_remote_edit()   (adapter — CollabObserver)
        │   prim_edit_to_transform(edit) via gxform  → Godot Transform3D
        │   emit_signal("transform_broadcast_received", prim_path, xform, from)
        ▼
[6] GDScript / UI listeners react (purely informational; the stage+node already moved in 5a)
```

---

**Spine-axis rotation.** For Cone/Cylinder the loader (UsdGodotTypeConverter::toTransform)
bakes a presentation rotation into the Godot node basis for a non-default axis (X or Z),
because Godot's primitive meshes are Y-spined. The authoring path inverts that bake so USD
stays raw: StageBridge::spine_axis_for(prim_path) reads the axis from the live stage
(IsA<UsdGeomCone|UsdGeomCylinder> then GetAxisAttr, else None), author_to_usd strips it via
xform::strip_spine_axis before writing (so both the outbound author and the inbound apply store
the raw orientation), and apply_remote_edit re-applies it via xform::apply_spine_axis for
the node set_transform. read_prim_transform (the broadcast read) stays raw. The apply/strip
convention is byte-identical to toTransform (X->rot_z(+90), Z->rot_x(+90), Y/None->identity). Scope
is Cone/Cylinder only, because those are the only prim types the converter bakes a rotation for;
the mechanism is a no-op (None) for every other type, and Capsule has no visual converter in either
direction yet.
