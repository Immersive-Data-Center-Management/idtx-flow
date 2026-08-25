# IDTX-Core E2E test harness

Headless Python harness that exercises the IDTX-Core `/api/v1` collaboration
contract, and provides the "second client" tools needed for the steps that
require the live Godot editor.

- **No `protoc` / generated bindings** — `proto_codec.py` hand-implements the
  tiny protobuf wire format for exactly the IDTX messages (`BaseMessage`,
  `TransformUpdate`, `Matrix4dTransform`, `Vector3`, `Handshake`, `Ack`,
  `Error`, …), matching the collaboration protocol byte-for-byte (including the
  intentional `seperate` field typo).
- **Only two deps**: `requests`, `websocket-client`.

## What it covers

| Steps                               | Coverage |
|-------------------------------------|---|
| 1 health                            | ✅ automated (`full`) |
| 2 login (happy)                     | ✅ automated |
| 3 login (bad password → 401)        | ✅ automated |
| 4 file list                         | ✅ automated |
| 5 session create (201, single_edit) | ✅ automated |
| 6 stage load / handshake            | ✅ WS handshake automated; **visible prims** = manual (Godot) |
| 7 outbound sync (gizmo → broadcast) | 🔶 manual drag in Godot + `watch` prints the broadcasts |
| 8 inbound sync (apply remote)       | 🔶 `send-xform` sends a rotation; **watch it apply** in Godot |
| 9 single-edit lockout               | ✅ automated (2nd WS rejected/closed) |
| 10 teardown / no dangling           | ✅ automated (DELETE + GET==404) |

`full` returns exit code `0` if all automated checks pass, `1` otherwise
(so it can gate CI later).

## Which session modes support a 2nd client? (`probe-modes`)

`single_edit` allows **exactly one** WS client, so the harness cannot join the
editor's live session for steps 7/8 — the backend rejects the 2nd connection
with `400` (that's the same rule that powers the step-9 lockout). To find out
which modes *do* allow a second client:

```bash
python idtx_e2e.py probe-modes
```

It creates a session in each mode, opens ws#1, tries a 2nd client while ws#1 is
open, and prints a table:

```
mode                    create              ws#1      ws#2 (2nd client)
------------------------------------------------------------------------------------
single_edit             201                 ok(handshake)  REJECTED (... 400 ...)
single_runtime          400 invalid_request -              -
collaborative_edit      201                 ok(handshake)  ALLOWED (stayed open)
collaborative_runtime   ...                 ...            ...
```

A row whose **ws#2 = ALLOWED** is the mode to use for the visual steps 7/8.

## Setup

```bash
cd tests/e2e

# (recommended) a virtualenv
python -m venv .venv
# Windows:
.venv\Scripts\activate
# macOS/Linux:
# source .venv/bin/activate

pip install -r requirements.txt

# credentials/base-url (or pass --user/--password/--base-url on the CLI)
copy .env.example .env      # Windows
# cp .env.example .env      # macOS/Linux
#   then edit .env: IDTX_USER, IDTX_PASSWORD, IDTX_BASE_URL
```

`.env` is gitignored — never commit real credentials.

Prereq: a running IDTX-Core at `IDTX_BASE_URL` (default `http://localhost:8080`)
with at least one `.usda` under its uploads root (already the case here).

## Run the automated sequence (checks 1–6, 9, 10)

```bash
python idtx_e2e.py full
```

Optionally pin the file used for the session:

```bash
python idtx_e2e.py full --usd-file <your-file.usda>
```

Sample output:

```
IDTX-Core E2E — automated backend/wire checks (1-6, 9, 10)
  [PASS] 1. health — GET /api/v1/health == 200
  [PASS] 2. login-ok — 200 + access_token
  [PASS] 3. login-bad — 401 (invalid_credentials)
  [PASS] 4. files — GET /api/v1/files (>=1 .usd*)
  [PASS] 5. session — POST /api/v1/sessions (201, single_edit)
  [PASS] 6. ws-handshake — connect + receive Handshake
  [PASS] 9. single-edit lockout — 2nd WS rejected/closed
  [PASS] 10. teardown — DELETE session (204/404) + gone
RESULT: ALL AUTOMATED CHECKS PASSED
```

## The steps that need the Godot editor (6 visible, 7 outbound, 8 inbound)

These use a **live editor session**. `full` deletes the session it creates, so
for the manual steps you create the session **from Godot** (import an Asset
Server file), then attach the harness as a *second client* to that same session.

> **Prerequisite (steps 7 & 8):** the harness must be allowed to join the
> editor's session as a 2nd client. `single_edit` forbids that (see
> `probe-modes`). So first pick a mode whose `ws#2 = ALLOWED`, then tell the
> editor to open that mode:
>
> 1. `python idtx_e2e.py probe-modes` → find a mode with `ws#2 = ALLOWED`
>    (typically `collaborative_edit`).
> 2. In `addons/IDTXFlow/import_manager/import_manager.gd` set
>    `const DEBUG_COLLAB_MODE := true`, reload the plugin/project.
> 3. Re-import the Asset Server file. The editor now opens a `collaborative_edit`
>    session a 2nd client can join.
> 4. Set `DEBUG_COLLAB_MODE := false` again when done.

**Getting the session id:** it's printed in the Godot **Output** panel the
moment the session is created (from `import_manager.gd::_on_session_ready`):

```
[IDTXFlow] [Import Manager] Session created: session_id=<uuid>  ws_url=/ws?sid=<uuid>
[IDTXFlow] [Import Manager]   E2E: python idtx_e2e.py watch --sid <uuid>
```

Copy that `--sid` into the commands below.

**Prim paths** depend on your stage — use a leaf prim path from your own `.usda`
(a full USD path such as `/World/Sphere` or `/root/Foo/Bar`, whatever your stage
uses; there is no fixed root name). The `watch` output prints the prim of each
broadcast, and the `emit_outbound_once.gd` dev tool prints the tracked prim paths
for the loaded stage. (If the stage uses a small `metersPerUnit`, e.g. `0.01`,
translations are in cm — prefer rotation or large `--trans` values to see an
obvious change.)

### Step 7 — outbound (a local move leaves the client)

```bash
python idtx_e2e.py watch --sid <SESSION_ID>
```

The converted USD prims are **not** exposed as nodes in Godot's Scene tree (only
the `UsdStageNode3D` root is), so there is nothing to select and gizmo-drag. To
simulate a local edit leaving the Godot client, run the dev tool
`addons/IDTXFlow/tools/emit_outbound_once.gd` (open it in the Script editor →
**File ▸ Run**, `Ctrl+Shift+X`). It moves a converted prim and fires the same
production hook a real gizmo drag would, so the edit is authored → TfNotice →
sent on the wire. By default it auto-picks a leaf prim; set `TARGET_PRIM` in that
script to target a specific one (it prints the available prim paths when it runs).

With `watch` running, you should see `[broadcast #N] client='…' prim='…'` lines
with the decoded matrix — confirming the outbound path end to end.

> Note: in `single_edit`, the backend suppresses echoing your *own* update back
> to you, but a **separate** watcher client still receives the broadcast — which
> is exactly what this verifies.

### Step 8 — inbound (a remote edit applies in your editor)

With the editor session open (prims visible), send a rotation as a second client:

```bash
python idtx_e2e.py send-xform --sid <SESSION_ID> --prim <PRIM_PATH> --rot 0 45 0
```

Watch the Godot viewport: the targeted prim should rotate, and the client log
should show `transform_broadcast_received` / `apply_remote`. This exercises the
**fixed matrix path** on the inbound side (rotation, not just translation).

Variants:

```bash
# translate instead of rotate
python idtx_e2e.py send-xform --sid <SID> --prim <PRIM_PATH> --trans 1 0 0 --rot 0 0 0

# non-uniform scale + rotation
python idtx_e2e.py send-xform --sid <SID> --prim <PRIM_PATH> --rot 0 30 0 --scale 2 1 0.5

# use the SeparateTransform oneof instead of a 4x4 matrix
python idtx_e2e.py send-xform --sid <SID> --prim <PRIM_PATH> --separate --rot 0 45 0

# keep listening a few seconds after sending (see any ack/echo)
python idtx_e2e.py send-xform --sid <SID> --prim <PRIM_PATH> --rot 0 45 0 --wait 3
```

### Quick handshake sanity

```bash
python idtx_e2e.py handshake --sid <SESSION_ID>
```

## Notes & caveats

- **2nd client into a `single_edit` session:** driving inbound needs a second
  client (this harness, or curl/websocat). If your backend *rejects* a
  2nd connection to a `single_edit` session outright (the same rule that powers
  the single-edit lockout), `watch`/`send-xform` will report the rejection instead of
  hanging — in that case inbound testing requires a `collaborative_*` session
  mode. The harness prints a clear hint when this happens.
- **Matrix convention:** `send-xform --matrix` builds a row-major 4×4 with the
  translation in the last row, matching the client's wire layout
  (`IdtxClient::transform_to_mat4`) so the editor reconstructs the intended
  transform. `--separate` uses Euler degrees (USD convention).
- **Colors:** ANSI colors auto-enable on TTYs; set `FORCE_COLOR=1` to force.

## Files

| File | Purpose |
|---|---|
| `idtx_e2e.py` | the harness (argparse CLI: `full`, `handshake`, `watch`, `send-xform`) |
| `proto_codec.py` | pure-Python protobuf encode/decode for the IDTX messages |
| `requirements.txt` | `requests`, `websocket-client` |
| `.env.example` | copy to `.env`; `IDTX_BASE_URL` / `IDTX_USER` / `IDTX_PASSWORD` |
| `.gitignore` | keeps `.env`, `.venv/`, `__pycache__/` out of git |