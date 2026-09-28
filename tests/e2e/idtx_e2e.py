#!/usr/bin/env python3
"""
idtx_e2e.py — headless E2E harness for the IDTX-Core /api/v1 collaboration
contract: it automates the backend/wire acceptance checks and provides the
"second client" tools needed for the human-in-the-loop steps.

No protoc / generated bindings required — see proto_codec.py.

Dependencies:  requests, websocket-client   (see requirements.txt)

Config precedence for every option:  CLI flag  >  env var  >  default
    --base-url / IDTX_BASE_URL   (default http://localhost:8080)
    --user     / IDTX_USER
    --password / IDTX_PASSWORD

Subcommands:
    full            Run the automated backend/wire sequence (checks 1-6, 9, 10).
    probe-modes     Try every session mode; report which the backend accepts and
                    whether a 2nd WS client is allowed (decides steps 7/8 setup).
    handshake       Connect a WS client, print the Handshake, exit.
    watch           Connect a read-only 2nd client; print inbound broadcasts
                    (drag a gizmo in the Godot editor → step 7 outbound check).
    send-xform      Connect as a 2nd client; send one TransformUpdate (default a
                    45° Y rotation) → the Godot editor should apply it (step 8).

Examples:
    python idtx_e2e.py full
    python idtx_e2e.py probe-modes
    python idtx_e2e.py handshake --sid <session_id>
    python idtx_e2e.py watch --sid <session_id>
    python idtx_e2e.py send-xform --sid <session_id> --prim /World/Sphere --rot 0 45 0
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from typing import List, Optional, Tuple

try:
    import requests
except ImportError:
    print("ERROR: missing dependency 'requests'. Run: pip install -r requirements.txt", file=sys.stderr)
    sys.exit(2)

try:
    import websocket  # websocket-client
except ImportError:
    print("ERROR: missing dependency 'websocket-client'. Run: pip install -r requirements.txt", file=sys.stderr)
    sys.exit(2)

import proto_codec as pc


# ---------------------------------------------------------------------------
# Small console helpers
# ---------------------------------------------------------------------------

class C:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    CYAN = "\033[96m"
    DIM = "\033[2m"
    RESET = "\033[0m"


def _supports_color() -> bool:
    return sys.stdout.isatty() and os.name != "nt" or os.environ.get("FORCE_COLOR") == "1"


_COLOR = _supports_color()


def _c(text: str, color: str) -> str:
    return f"{color}{text}{C.RESET}" if _COLOR else text


def info(msg: str) -> None:
    print(msg)


def step_result(name: str, ok: bool, detail: str = "") -> bool:
    tag = _c("PASS", C.GREEN) if ok else _c("FAIL", C.RED)
    line = f"  [{tag}] {name}"
    if detail:
        line += f"  {_c('— ' + detail, C.DIM)}"
    print(line)
    return ok


# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

class Config:
    def __init__(self, base_url: str, user: str, password: str):
        self.base_url = base_url.rstrip("/")
        self.user = user
        self.password = password

    @property
    def ws_base(self) -> str:
        if self.base_url.startswith("https://"):
            return "wss://" + self.base_url[len("https://"):]
        if self.base_url.startswith("http://"):
            return "ws://" + self.base_url[len("http://"):]
        return self.base_url


def _load_dotenv_if_present() -> None:
    """Minimal .env loader (no python-dotenv dependency). Loads KEY=VALUE lines
    from a `.env` file next to this script into os.environ (without overriding
    variables already set in the real environment)."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env")
    if not os.path.isfile(path):
        return
    try:
        with open(path, "r", encoding="utf-8") as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, _, val = line.partition("=")
                key = key.strip()
                val = val.strip().strip('"').strip("'")
                if key and key not in os.environ:
                    os.environ[key] = val
    except OSError:
        pass


def resolve_config(args) -> Config:
    _load_dotenv_if_present()
    base = args.base_url or os.environ.get("IDTX_BASE_URL") or "http://localhost:8080"
    user = getattr(args, "user", None) or os.environ.get("IDTX_USER") or ""
    pw = getattr(args, "password", None) or os.environ.get("IDTX_PASSWORD") or ""
    return Config(base, user, pw)


# ---------------------------------------------------------------------------
# REST helpers
# ---------------------------------------------------------------------------

def rest_login(cfg: Config, username: str, password: str, timeout=15) -> Tuple[int, dict]:
    url = f"{cfg.base_url}/api/v1/auth/login"
    try:
        r = requests.post(url, json={"username": username, "password": password},
                          headers={"Accept": "application/json"}, timeout=timeout)
    except requests.RequestException as e:
        return 0, {"error": "transport_error", "message": str(e)}
    try:
        body = r.json()
    except ValueError:
        body = {}
    return r.status_code, body


def rest_get(cfg: Config, path: str, token: str, timeout=15) -> Tuple[int, dict]:
    try:
        r = requests.get(f"{cfg.base_url}{path}",
                         headers={"Accept": "application/json", "Authorization": f"Bearer {token}"},
                         timeout=timeout)
    except requests.RequestException as e:
        return 0, {"error": "transport_error", "message": str(e)}
    try:
        return r.status_code, r.json()
    except ValueError:
        return r.status_code, {}


def rest_post(cfg: Config, path: str, token: str, payload: dict, timeout=15) -> Tuple[int, dict]:
    try:
        r = requests.post(f"{cfg.base_url}{path}", json=payload,
                          headers={"Accept": "application/json", "Authorization": f"Bearer {token}"},
                          timeout=timeout)
    except requests.RequestException as e:
        return 0, {"error": "transport_error", "message": str(e)}
    try:
        return r.status_code, r.json()
    except ValueError:
        return r.status_code, {}


def rest_delete(cfg: Config, path: str, token: str, timeout=15) -> int:
    try:
        r = requests.delete(f"{cfg.base_url}{path}",
                            headers={"Authorization": f"Bearer {token}"}, timeout=timeout)
        return r.status_code
    except requests.RequestException:
        return 0


# ---------------------------------------------------------------------------
# WebSocket helpers
# ---------------------------------------------------------------------------

def ws_connect(cfg: Config, ws_url_rel: str, token: str, timeout=10) -> websocket.WebSocket:
    """Open a binary WS with the bearer header. ws_url_rel is like '/ws?sid=...'."""
    full = cfg.ws_base + ws_url_rel
    ws = websocket.WebSocket()
    ws.connect(full, header=[f"Authorization: Bearer {token}"], timeout=timeout)
    return ws


def ws_recv_base(ws: websocket.WebSocket, timeout: float) -> Optional[dict]:
    """Receive one frame and decode it as a BaseMessage. Returns None on timeout."""
    ws.settimeout(timeout)
    try:
        frame = ws.recv()
    except websocket.WebSocketTimeoutException:
        return None
    except (websocket.WebSocketConnectionClosedException, OSError):
        return {"kind": "_closed", "payload": None, "session_id": ""}
    if isinstance(frame, str):
        frame = frame.encode("latin-1", errors="ignore")
    if not frame:
        return None
    try:
        return pc.decode_base_message(frame)
    except Exception as e:  # noqa: BLE001
        return {"kind": "_undecodable", "payload": {"error": str(e), "len": len(frame)}, "session_id": ""}


# ---------------------------------------------------------------------------
# Matrix helper (row-major m00..m33), a Y-axis rotation by default
# ---------------------------------------------------------------------------

def make_matrix(rot_deg: Tuple[float, float, float],
                trans: Tuple[float, float, float],
                scale: Tuple[float, float, float]) -> List[float]:
    """Build a row-major 4x4 (m00..m33) from euler(deg) XYZ, translation, scale.

    Convention here mirrors the client's wire form (row-major, translation in the
    last row m30..m32). Rotation order applied X then Y then Z (R = Rz*Ry*Rx),
    matching Godot's Basis.from_euler default.
    """
    import math
    rx, ry, rz = (math.radians(a) for a in rot_deg)
    sx, sy, sz = scale

    cx, cxs = math.cos(rx), math.sin(rx)
    cy, cys = math.cos(ry), math.sin(ry)
    cz, czs = math.cos(rz), math.sin(rz)

    # Rz * Ry * Rx (column-vector convention), 3x3
    r00 = cz * cy
    r01 = cz * cys * cxs - czs * cx
    r02 = cz * cys * cx + czs * cxs
    r10 = czs * cy
    r11 = czs * cys * cxs + cz * cx
    r12 = czs * cys * cx - cz * cxs
    r20 = -cys
    r21 = cy * cxs
    r22 = cy * cx

    # apply scale on the columns (axis vectors)
    r00 *= sx; r10 *= sx; r20 *= sx
    r01 *= sy; r11 *= sy; r21 *= sy
    r02 *= sz; r12 *= sz; r22 *= sz

    tx, ty, tz = trans
    # Row-major with translation in the last ROW (m30..m32), matching the client's
    # transform_to_mat4 wire layout (Godot Basis row i -> wire row i).
    return [
        r00, r01, r02, 0.0,
        r10, r11, r12, 0.0,
        r20, r21, r22, 0.0,
        tx,  ty,  tz,  1.0,
    ]


def fmt_matrix(m: List[float]) -> str:
    def row(i):
        return "[" + ", ".join(f"{m[i*4+j]:.4f}" for j in range(4)) + "]"
    return "\n    ".join(row(i) for i in range(4))


# ---------------------------------------------------------------------------
# Subcommand: full
# ---------------------------------------------------------------------------

def cmd_full(cfg: Config, args) -> int:
    print(_c("IDTX-Core E2E — automated backend/wire checks (1-6, 9, 10)", C.CYAN))
    print(f"  base_url = {cfg.base_url}")
    print(f"  ws_base  = {cfg.ws_base}")
    print(f"  user     = {cfg.user or '(unset)'}")
    print("")

    if not cfg.user or not cfg.password:
        print(_c("ERROR: username/password required (--user/--password or IDTX_USER/IDTX_PASSWORD).", C.RED))
        return 2

    all_ok = True

    # 1. Health
    code, _ = rest_get(cfg, "/api/v1/health", token="")
    all_ok &= step_result("1. health — GET /api/v1/health == 200", code == 200, f"http={code}")

    # 2. Login (happy)
    code, body = rest_login(cfg, cfg.user, cfg.password)
    token = body.get("access_token", "") if code == 200 else ""
    ok = (code == 200 and bool(token))
    all_ok &= step_result("2. login-ok — 200 + access_token", ok,
                          f"http={code} token={'yes' if token else 'no'}")
    if not token:
        print(_c("  Cannot continue without a token; aborting remaining steps.", C.RED))
        return 1

    # 3. Login (bad password)
    code, body = rest_login(cfg, cfg.user, cfg.password + "_definitely_wrong")
    err = str(body.get("error", ""))
    ok = (code == 401)
    all_ok &= step_result("3. login-bad — 401 (invalid_credentials)", ok,
                          f"http={code} error='{err}'")

    # 4. File list
    code, body = rest_get(cfg, "/api/v1/files", token)
    files = body.get("files", []) if isinstance(body, dict) else []
    usd_files = [f for f in files if str(f.get("filename", "")).lower().endswith((".usd", ".usda", ".usdc", ".usdz"))]
    picked = None
    if args.usd_file:
        for f in files:
            if f.get("filepath") == args.usd_file or f.get("filename") == args.usd_file:
                picked = f
                break
        if picked is None and usd_files:
            print(_c(f"  (requested --usd-file '{args.usd_file}' not found; using first listed)", C.YELLOW))
    if picked is None and usd_files:
        picked = usd_files[0]
    ok = (code == 200 and len(usd_files) > 0)
    all_ok &= step_result("4. files — GET /api/v1/files (>=1 .usd*)", ok,
                          f"http={code} count={len(files)} usd={len(usd_files)} pick='{picked.get('filepath') if picked else None}'")
    if picked is None:
        print(_c("  No USD file available; cannot create a session. Aborting remaining steps.", C.RED))
        return 1

    usd_file = picked.get("filepath") or picked.get("filename")

    # 5. Session create
    req_mode = getattr(args, "mode", "single_edit") or "single_edit"
    code, body = rest_post(cfg, "/api/v1/sessions", token, {"usd_file": usd_file, "mode": req_mode})
    session_id = body.get("session_id", "") if isinstance(body, dict) else ""
    mode = body.get("mode", "") if isinstance(body, dict) else ""
    ws_rel = body.get("ws_url", "") if isinstance(body, dict) else ""
    ok = (code == 201 and bool(session_id) and mode == req_mode and bool(ws_rel))
    all_ok &= step_result(f"5. session — POST /api/v1/sessions (201, {req_mode})", ok,
                          f"http={code} sid={session_id[:8] + '…' if session_id else '(none)'} mode='{mode}' ws='{ws_rel}'")
    if not session_id or not ws_rel:
        print(_c("  No session/ws_url; cannot exercise WS steps. Aborting remaining steps.", C.RED))
        return 1

    # 6. WS handshake
    hs_ok = False
    hs_detail = ""
    ws1 = None
    try:
        ws1 = ws_connect(cfg, ws_rel, token)
        msg = ws_recv_base(ws1, timeout=args.ws_timeout)
        if msg is None:
            hs_detail = "no handshake within timeout"
        elif msg.get("kind") == "handshake":
            hs = msg["payload"]
            match_sid = (hs.get("session_id") == session_id) or (msg.get("session_id") == session_id)
            hs_ok = True  # got a handshake at all
            hs_detail = (f"usd_path='{hs.get('usd_path')}' usd_uri='{hs.get('usd_uri')}' "
                         f"sid_match={'yes' if match_sid else 'no'}")
        else:
            hs_detail = f"first frame was '{msg.get('kind')}', expected handshake"
    except Exception as e:  # noqa: BLE001
        hs_detail = f"connect failed: {e}"
    all_ok &= step_result("6. ws-handshake — connect + receive Handshake", hs_ok, hs_detail)

    # 9. Single-edit lockout: a SECOND ws to the same sid should be rejected.
    lock_ok = False
    lock_detail = ""
    ws2 = None
    try:
        ws2 = ws_connect(cfg, ws_rel, token)
        # Either the upgrade is refused (exception) or we get an immediate close/error frame.
        msg = ws_recv_base(ws2, timeout=args.ws_timeout)
        if msg is None:
            lock_detail = "2nd client connected and stayed open (no lockout observed within timeout)"
            lock_ok = False
        elif msg.get("kind") in ("_closed",):
            lock_ok = True
            lock_detail = "2nd client closed by server"
        elif msg.get("kind") == "error":
            lock_ok = True
            lock_detail = f"error code='{msg['payload'].get('code')}' msg='{msg['payload'].get('message')}'"
        else:
            lock_detail = f"2nd client got '{msg.get('kind')}' (expected close/error)"
            lock_ok = False
    except websocket.WebSocketBadStatusException as e:
        # HTTP-level rejection of the upgrade — this is the expected single_edit_busy path.
        lock_ok = True
        lock_detail = f"upgrade rejected: {e}"
    except Exception as e:  # noqa: BLE001
        # A close during/just after handshake also counts as a lockout signal.
        lock_ok = True
        lock_detail = f"2nd connect raised: {e}"
    finally:
        if ws2 is not None:
            try:
                ws2.close()
            except Exception:  # noqa: BLE001
                pass
    all_ok &= step_result("9. single-edit lockout — 2nd WS rejected/closed", lock_ok, lock_detail)

    # Close the first WS before teardown.
    if ws1 is not None:
        try:
            ws1.close()
        except Exception:  # noqa: BLE001
            pass

    # 10. Teardown
    del_code = rest_delete(cfg, f"/api/v1/sessions/{session_id}", token)
    del_ok = del_code in (204, 200, 404)
    detail = f"http={del_code}"
    # Best-effort: confirm it's gone if a GET-by-id exists.
    code, _ = rest_get(cfg, f"/api/v1/sessions/{session_id}", token)
    if code == 404:
        detail += " (confirmed gone: GET==404)"
    elif code == 200:
        detail += _c(" (WARNING: still present: GET==200)", C.YELLOW)
        del_ok = False
    all_ok &= step_result("10. teardown — DELETE session (204/404) + gone", del_ok, detail)

    print("")
    if all_ok:
        print(_c("RESULT: ALL AUTOMATED CHECKS PASSED", C.GREEN))
    else:
        print(_c("RESULT: ONE OR MORE CHECKS FAILED", C.RED))
    print(_c(
        "\nNote: steps 6(visible prims), 7(gizmo→outbound) and 8(inbound apply)\n"
        "require the Godot editor + your eye. Use the 'watch' and 'send-xform'\n"
        "subcommands against a live editor session (see README).", C.DIM))
    return 0 if all_ok else 1


# ---------------------------------------------------------------------------
# Subcommand: handshake
# ---------------------------------------------------------------------------

def _login_or_die(cfg: Config) -> str:
    if not cfg.user or not cfg.password:
        print(_c("ERROR: username/password required (--user/--password or IDTX_USER/IDTX_PASSWORD).", C.RED))
        sys.exit(2)
    code, body = rest_login(cfg, cfg.user, cfg.password)
    token = body.get("access_token", "") if code == 200 else ""
    if not token:
        print(_c(f"ERROR: login failed (http={code}, error='{body.get('error')}').", C.RED))
        sys.exit(1)
    return token


def _ws_rel_for_sid(sid: str, explicit_ws_url: str = "") -> str:
    if explicit_ws_url:
        return explicit_ws_url
    return f"/ws?sid={sid}"


def cmd_handshake(cfg: Config, args) -> int:
    token = _login_or_die(cfg)
    ws_rel = _ws_rel_for_sid(args.sid, args.ws_url)
    print(f"Connecting to {cfg.ws_base}{ws_rel} …")
    ws = ws_connect(cfg, ws_rel, token)
    msg = ws_recv_base(ws, timeout=args.ws_timeout)
    try:
        ws.close()
    except Exception:  # noqa: BLE001
        pass
    if msg is None:
        print(_c("No frame received within timeout.", C.YELLOW))
        return 1
    print(f"kind = {msg.get('kind')}")
    print(f"session_id(envelope) = {msg.get('session_id')}")
    print(f"payload = {msg.get('payload')}")
    return 0 if msg.get("kind") == "handshake" else 1


# ---------------------------------------------------------------------------
# Subcommand: watch
# ---------------------------------------------------------------------------

def _print_connect_failure_hint(e: Exception) -> None:
    print(_c(f"Connect failed: {e}", C.RED))
    is_400 = isinstance(e, websocket.WebSocketBadStatusException) and "400" in str(e)
    if is_400:
        print(_c(
            "single_edit allows only ONE client — a 2nd client (this harness) is rejected\n"
            "with 400 while the Godot editor holds the single seat. To test steps 7/8:\n"
            "  • run `python idtx_e2e.py probe-modes` to see which modes accept a 2nd client, then\n"
            "  • set DEBUG_COLLAB_MODE := true in import_manager.gd, reload the plugin, re-import,\n"
            "    and join with the same --sid; OR drive the editor's own single client directly.",
            C.YELLOW))
    else:
        print(_c("Check --sid is the CURRENT editor session id and the backend is reachable.", C.YELLOW))


def cmd_watch(cfg: Config, args) -> int:
    token = _login_or_die(cfg)
    ws_rel = _ws_rel_for_sid(args.sid, args.ws_url)
    print(f"Connecting (read-only 2nd client) to {cfg.ws_base}{ws_rel} …")
    try:
        ws = ws_connect(cfg, ws_rel, token)
    except Exception as e:  # noqa: BLE001
        _print_connect_failure_hint(e)
        return 1

    print(_c("Watching for broadcasts. Drag a prim's gizmo in the Godot editor.\n"
             "Press Ctrl+C to stop.\n", C.CYAN))
    count = 0
    try:
        while True:
            msg = ws_recv_base(ws, timeout=1.0)
            if msg is None:
                continue
            kind = msg.get("kind")
            if kind == "handshake":
                print(_c(f"[handshake] {msg['payload']}", C.DIM))
            elif kind == "xform_broadcast":
                b = msg["payload"]
                upd = b.get("update") or {}
                count += 1
                print(_c(f"[broadcast #{count}] client='{b.get('client_id')}' prim='{upd.get('prim_path')}'", C.GREEN))
                if upd.get("matrix") is not None:
                    print("    matrix (row-major):\n    " + fmt_matrix(upd["matrix"]))
                elif upd.get("separate") is not None:
                    print(f"    separate: {upd['separate']}")
            elif kind == "ack":
                print(_c(f"[ack] {msg['payload']}", C.DIM))
            elif kind == "error":
                print(_c(f"[error] {msg['payload']}", C.YELLOW))
            elif kind == "_closed":
                print(_c("[closed] server closed the connection", C.YELLOW))
                break
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        try:
            ws.close()
        except Exception:  # noqa: BLE001
            pass
    print(f"Total broadcasts observed: {count}")
    return 0


# ---------------------------------------------------------------------------
# Subcommand: send-xform
# ---------------------------------------------------------------------------

def cmd_send_xform(cfg: Config, args) -> int:
    token = _login_or_die(cfg)
    ws_rel = _ws_rel_for_sid(args.sid, args.ws_url)
    print(f"Connecting (2nd client, sender) to {cfg.ws_base}{ws_rel} …")
    try:
        ws = ws_connect(cfg, ws_rel, token)
    except Exception as e:  # noqa: BLE001
        _print_connect_failure_hint(e)
        return 1

    # Drain the initial handshake (best effort).
    hs = ws_recv_base(ws, timeout=args.ws_timeout)
    if hs and hs.get("kind") == "handshake":
        print(_c(f"[handshake] usd_path='{hs['payload'].get('usd_path')}'", C.DIM))

    rot = tuple(args.rot)
    trans = tuple(args.trans)
    scale = tuple(args.scale)

    if args.separate:
        update = pc.encode_transform_update(
            prim_path=args.prim,
            session_id=args.sid,
            separate={"translation": list(trans), "rotation": list(rot), "scale": list(scale)},
            timestamp=int(time.time() * 1000),
        )
        print(_c(f"Sending SEPARATE update to prim='{args.prim}':", C.CYAN))
        print(f"    translation={trans}  rotation(deg)={rot}  scale={scale}")
    else:
        m = make_matrix(rot, trans, scale)
        update = pc.encode_transform_update(
            prim_path=args.prim,
            session_id=args.sid,
            matrix16=m,
            timestamp=int(time.time() * 1000),
        )
        print(_c(f"Sending MATRIX update to prim='{args.prim}':", C.CYAN))
        print("    " + fmt_matrix(m))

    base = pc.encode_base_with_xform_update(update, session_id=args.sid)
    try:
        ws.send_binary(base)
    except AttributeError:
        # older websocket-client: send with opcode
        ws.send(base, opcode=websocket.ABNF.OPCODE_BINARY)
    print(_c(f"Sent {len(base)} bytes. Check the Godot editor: prim '{args.prim}' should move/rotate,", C.GREEN))
    print(_c("and the client log should show transform_broadcast_received / apply_remote.", C.GREEN))

    # Optionally wait a moment for an ack/echo.
    if args.wait > 0:
        deadline = time.time() + args.wait
        while time.time() < deadline:
            msg = ws_recv_base(ws, timeout=0.5)
            if msg is None:
                continue
            print(_c(f"[recv] kind={msg.get('kind')} payload={msg.get('payload')}", C.DIM))

    try:
        ws.close()
    except Exception:  # noqa: BLE001
        pass
    return 0


# ---------------------------------------------------------------------------
# Subcommand: probe-modes
# ---------------------------------------------------------------------------

ALL_MODES = ["single_edit", "single_runtime", "collaborative_edit", "collaborative_runtime"]


def _pick_usd_file(cfg: Config, token: str, prefer: Optional[str]) -> Optional[str]:
    code, body = rest_get(cfg, "/api/v1/files", token)
    files = body.get("files", []) if isinstance(body, dict) else []
    usd = [f for f in files if str(f.get("filename", "")).lower().endswith((".usd", ".usda", ".usdc", ".usdz"))]
    if prefer:
        for f in files:
            if f.get("filepath") == prefer or f.get("filename") == prefer:
                return f.get("filepath") or f.get("filename")
    if usd:
        return usd[0].get("filepath") or usd[0].get("filename")
    return None


def _try_second_client(cfg: Config, ws_rel: str, token: str, ws_timeout: float) -> str:
    """Open a 2nd WS to the same session and report allowed/rejected."""
    try:
        ws2 = ws_connect(cfg, ws_rel, token, timeout=min(ws_timeout, 6))
    except websocket.WebSocketBadStatusException as e:
        return f"REJECTED ({e})"
    except Exception as e:  # noqa: BLE001
        return f"REJECTED ({type(e).__name__}: {e})"
    # Connected — see if it stays open or gets an immediate close/error.
    msg = ws_recv_base(ws2, timeout=min(ws_timeout, 4))
    try:
        ws2.close()
    except Exception:  # noqa: BLE001
        pass
    if msg is None:
        return "ALLOWED (stayed open)"
    if msg.get("kind") == "_closed":
        return "REJECTED (server closed)"
    if msg.get("kind") == "error":
        return f"REJECTED (error {msg['payload'].get('code')})"
    return f"ALLOWED (got '{msg.get('kind')}')"


def cmd_probe_modes(cfg: Config, args) -> int:
    token = _login_or_die(cfg)
    usd_file = _pick_usd_file(cfg, token, args.usd_file)
    if not usd_file:
        print(_c("No USD file available to probe with.", C.RED))
        return 1

    print(_c(f"Probing session modes against {cfg.base_url} using usd_file='{usd_file}'", C.CYAN))
    print(f"{'mode':<24}{'create':<20}{'ws#1':<10}{'ws#2 (2nd client)':<30}")
    print("-" * 84)

    for mode in ALL_MODES:
        code, body = rest_post(cfg, "/api/v1/sessions", token, {"usd_file": usd_file, "mode": mode})
        create = f"{code}"
        if isinstance(body, dict) and body.get("error"):
            create += f" {body.get('error')}"
        sid = body.get("session_id", "") if isinstance(body, dict) else ""
        ws_rel = body.get("ws_url", "") if isinstance(body, dict) else ""
        got_mode = body.get("mode", "") if isinstance(body, dict) else ""

        ws1 = "-"
        ws2 = "-"
        if code in (200, 201) and ws_rel:
            # ws#1
            try:
                w1 = ws_connect(cfg, ws_rel, token, timeout=min(args.ws_timeout, 6))
                m = ws_recv_base(w1, timeout=min(args.ws_timeout, 4))
                ws1 = "ok(handshake)" if (m and m.get("kind") == "handshake") else f"open({m.get('kind') if m else 'none'})"
                # ws#2 while ws1 is still open (that's the real lockout test)
                ws2 = _try_second_client(cfg, ws_rel, token, args.ws_timeout)
                try:
                    w1.close()
                except Exception:  # noqa: BLE001
                    pass
            except websocket.WebSocketBadStatusException as e:
                ws1 = f"REJECTED ({e})"
            except Exception as e:  # noqa: BLE001
                ws1 = f"err({type(e).__name__})"
            # cleanup
            if sid:
                rest_delete(cfg, f"/api/v1/sessions/{sid}", token)

        mode_label = mode + (f"→{got_mode}" if got_mode and got_mode != mode else "")
        print(f"{mode_label:<24}{create:<20}{ws1:<10}{ws2:<30}")

    print("")
    print(_c("Interpretation:", C.CYAN))
    print("  - A mode where ws#2 shows ALLOWED supports a 2nd client → use it for")
    print("    the harness `watch`/`send-xform` inbound/outbound tests (set the")
    print("    editor's DEBUG_COLLAB_MODE to match, then join with the same --sid).")
    print("  - If only single_edit works and ws#2 is REJECTED everywhere, inbound")
    print("    visual testing needs the editor's own single client (the C++")
    print("    self-test already validates the matrix path).")
    return 0


# ---------------------------------------------------------------------------
# argparse
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="idtx_e2e.py",
        description="Headless E2E harness for IDTX-Core /api/v1 collaboration.",
        epilog=("Note: --prim/--rot/--trans/--scale/--separate belong to the 'send-xform' "
                "subcommand ONLY. 'watch' and 'handshake' are receive-only and take just "
                "--sid (and optional --ws-url)."),
    )
    p.add_argument("--base-url", default=None, help="Backend base URL (env IDTX_BASE_URL; default http://localhost:8080)")
    p.add_argument("--user", default=None, help="Login username (env IDTX_USER)")
    p.add_argument("--password", default=None, help="Login password (env IDTX_PASSWORD)")
    p.add_argument("--ws-timeout", type=float, default=8.0, help="Seconds to wait for WS frames (default 8)")

    sub = p.add_subparsers(dest="command", required=True)

    pf = sub.add_parser("full", help="Run automated backend/wire checks (1-6, 9, 10).")
    pf.add_argument("--usd-file", default=None, help="Prefer this filepath/filename for the session (else first .usd*).")
    pf.add_argument("--mode", default="single_edit",
                    help="Session mode to create (default single_edit; e.g. collaborative_edit).")
    pf.set_defaults(func=cmd_full)

    pm = sub.add_parser("probe-modes", help="Try every session mode; report which the backend accepts and whether a 2nd WS client is allowed.")
    pm.add_argument("--usd-file", default=None, help="Prefer this filepath/filename (else first .usd*).")
    pm.set_defaults(func=cmd_probe_modes)

    ph = sub.add_parser("handshake", help="Connect a WS client, print the Handshake, exit.")
    ph.add_argument("--sid", required=True, help="Session id (from create/session_created).")
    ph.add_argument("--ws-url", default="", help="Explicit relative ws url (default /ws?sid=<sid>).")
    ph.set_defaults(func=cmd_handshake)

    pw = sub.add_parser("watch",
                        help="Read-only 2nd client; print inbound broadcasts (step 7 outbound check).",
                        epilog="Receive-only: takes --sid (and optional --ws-url). It does NOT accept --prim/--rot/etc; use 'send-xform' for those.")
    pw.add_argument("--sid", required=True, help="Session id.")
    pw.add_argument("--ws-url", default="", help="Explicit relative ws url (default /ws?sid=<sid>).")
    pw.set_defaults(func=cmd_watch)

    ps = sub.add_parser("send-xform", help="2nd client; send one TransformUpdate (step 8 inbound check).")
    ps.add_argument("--sid", required=True, help="Session id.")
    ps.add_argument("--ws-url", default="", help="Explicit relative ws url (default /ws?sid=<sid>).")
    ps.add_argument("--prim", default="/World/Sphere", help="Target prim path (default /World/Sphere).")
    ps.add_argument("--separate", action="store_true", help="Send SeparateTransform instead of a matrix.")
    ps.add_argument("--rot", nargs=3, type=float, default=[0.0, 45.0, 0.0], metavar=("RX", "RY", "RZ"),
                    help="Euler rotation in degrees (default 0 45 0).")
    ps.add_argument("--trans", nargs=3, type=float, default=[0.0, 0.0, 0.0], metavar=("TX", "TY", "TZ"),
                    help="Translation (default 0 0 0).")
    ps.add_argument("--scale", nargs=3, type=float, default=[1.0, 1.0, 1.0], metavar=("SX", "SY", "SZ"),
                    help="Scale (default 1 1 1).")
    ps.add_argument("--wait", type=float, default=0.0, help="Seconds to keep reading after sending (default 0).")
    ps.set_defaults(func=cmd_send_xform)

    return p


def main(argv: Optional[List[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    cfg = resolve_config(args)
    try:
        return args.func(cfg, args)
    except KeyboardInterrupt:
        print("\nInterrupted.")
        return 130


if __name__ == "__main__":
    sys.exit(main())
