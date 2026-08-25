"""
proto_codec.py — minimal, self-contained protobuf (proto3) wire codec for the
IDTX collaboration messages, so the E2E harness needs NO protoc / generated
bindings.

It hand-encodes/decodes exactly the messages the client and backend exchange over
the collaboration protocol:

  Vector3           { double x=1; y=2; z=3; }
  Matrix4dTransform { double m00..m33 = fields 1..16 (row-major) }
  SeparateTransform { Vector3 translation=1; rotation=2; scale=3; }
  TransformUpdate   { string session_id=1; usd_file=2; prim_path=3;
                      oneof { SeparateTransform seperate=4; Matrix4dTransform matrix=5; }
                      int64 timestamp=7; }
  TransformBroadcast{ string client_id=1; TransformUpdate update=2; }
  Handshake         { string session_id=1; session_layer=2; usd_path=3; usd_uri=4; }
  Ack               { string ref_id=1; bool ok=2; string error=3; }
  Error             { string code=1; string message=2; }
  BaseMessage       { optional string session_id=1;
                      oneof { Handshake handshake=2; TransformUpdate xform_update=3;
                              TransformBroadcast xform_broadcast=4; Ack ack=5; Error error=6; } }

NOTE: the field name `seperate` (sic) is intentional and matches the backend.

Wire-type reference (proto3):
  0 = varint (int32/int64/bool/enum)
  1 = 64-bit (fixed64/double)
  2 = length-delimited (string/bytes/embedded message)
  5 = 32-bit (fixed32/float)
"""

from __future__ import annotations

import struct
from typing import Dict, List, Optional, Tuple


# ---------------------------------------------------------------------------
# Low-level wire primitives
# ---------------------------------------------------------------------------

def _encode_varint(value: int) -> bytes:
    if value < 0:
        # two's complement in 64 bits (matches protobuf int64 negative encoding)
        value &= (1 << 64) - 1
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        if value:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)


def _decode_varint(buf: bytes, pos: int) -> Tuple[int, int]:
    result = 0
    shift = 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
    return result, pos


def _tag(field: int, wire_type: int) -> bytes:
    return _encode_varint((field << 3) | wire_type)


def _encode_double(field: int, value: float) -> bytes:
    return _tag(field, 1) + struct.pack("<d", value)


def _encode_string(field: int, value: str) -> bytes:
    raw = value.encode("utf-8")
    return _tag(field, 2) + _encode_varint(len(raw)) + raw


def _encode_bool(field: int, value: bool) -> bytes:
    return _tag(field, 0) + _encode_varint(1 if value else 0)


def _encode_int64(field: int, value: int) -> bytes:
    return _tag(field, 0) + _encode_varint(value)


def _encode_message(field: int, body: bytes) -> bytes:
    return _tag(field, 2) + _encode_varint(len(body)) + body


def _iter_fields(buf: bytes):
    """Yield (field_number, wire_type, value, next_pos). value is:
       - int for varint
       - bytes(8) for 64-bit
       - bytes for length-delimited
       - bytes(4) for 32-bit
    """
    pos = 0
    n = len(buf)
    while pos < n:
        key, pos = _decode_varint(buf, pos)
        field = key >> 3
        wt = key & 0x07
        if wt == 0:
            val, pos = _decode_varint(buf, pos)
            yield field, wt, val
        elif wt == 1:
            val = buf[pos:pos + 8]
            pos += 8
            yield field, wt, val
        elif wt == 2:
            ln, pos = _decode_varint(buf, pos)
            val = buf[pos:pos + ln]
            pos += ln
            yield field, wt, val
        elif wt == 5:
            val = buf[pos:pos + 4]
            pos += 4
            yield field, wt, val
        else:
            raise ValueError(f"Unsupported wire type {wt} for field {field}")


def _as_double(raw: bytes) -> float:
    return struct.unpack("<d", raw)[0]


def _as_str(raw: bytes) -> str:
    return raw.decode("utf-8", errors="replace")


# ---------------------------------------------------------------------------
# Message encoders
# ---------------------------------------------------------------------------

def encode_matrix4d(m16: List[float]) -> bytes:
    """m16 is 16 doubles in row-major order (m00..m33)."""
    assert len(m16) == 16
    body = bytearray()
    for i, v in enumerate(m16):
        body += _encode_double(i + 1, float(v))
    return bytes(body)


def encode_vector3(x: float, y: float, z: float) -> bytes:
    return _encode_double(1, x) + _encode_double(2, y) + _encode_double(3, z)


def encode_separate(translation, rotation, scale) -> bytes:
    body = bytearray()
    body += _encode_message(1, encode_vector3(*translation))
    body += _encode_message(2, encode_vector3(*rotation))
    body += _encode_message(3, encode_vector3(*scale))
    return bytes(body)


def encode_transform_update(
    prim_path: str,
    session_id: str = "",
    usd_file: str = "",
    matrix16: Optional[List[float]] = None,
    separate: Optional[dict] = None,
    timestamp: int = 0,
) -> bytes:
    body = bytearray()
    if session_id:
        body += _encode_string(1, session_id)
    if usd_file:
        body += _encode_string(2, usd_file)
    body += _encode_string(3, prim_path)
    if matrix16 is not None:
        body += _encode_message(5, encode_matrix4d(matrix16))
    elif separate is not None:
        body += _encode_message(
            4,
            encode_separate(separate["translation"], separate["rotation"], separate["scale"]),
        )
    if timestamp:
        body += _encode_int64(7, timestamp)
    return bytes(body)


def encode_base_with_xform_update(update_body: bytes, session_id: str = "") -> bytes:
    """BaseMessage { session_id=1; xform_update=3 }"""
    body = bytearray()
    if session_id:
        body += _encode_string(1, session_id)
    body += _encode_message(3, update_body)
    return bytes(body)


# ---------------------------------------------------------------------------
# Message decoders (only what the harness needs to inspect)
# ---------------------------------------------------------------------------

def decode_vector3(buf: bytes) -> Tuple[float, float, float]:
    x = y = z = 0.0
    for field, wt, val in _iter_fields(buf):
        if field == 1 and wt == 1:
            x = _as_double(val)
        elif field == 2 and wt == 1:
            y = _as_double(val)
        elif field == 3 and wt == 1:
            z = _as_double(val)
    return x, y, z


def decode_matrix4d(buf: bytes) -> List[float]:
    m = [0.0] * 16
    for field, wt, val in _iter_fields(buf):
        if 1 <= field <= 16 and wt == 1:
            m[field - 1] = _as_double(val)
    return m


def decode_separate(buf: bytes) -> dict:
    out = {"translation": (0.0, 0.0, 0.0), "rotation": (0.0, 0.0, 0.0), "scale": (1.0, 1.0, 1.0)}
    for field, wt, val in _iter_fields(buf):
        if wt != 2:
            continue
        if field == 1:
            out["translation"] = decode_vector3(val)
        elif field == 2:
            out["rotation"] = decode_vector3(val)
        elif field == 3:
            out["scale"] = decode_vector3(val)
    return out


def decode_transform_update(buf: bytes) -> dict:
    out = {
        "session_id": "",
        "usd_file": "",
        "prim_path": "",
        "matrix": None,
        "separate": None,
        "timestamp": 0,
    }
    for field, wt, val in _iter_fields(buf):
        if field == 1 and wt == 2:
            out["session_id"] = _as_str(val)
        elif field == 2 and wt == 2:
            out["usd_file"] = _as_str(val)
        elif field == 3 and wt == 2:
            out["prim_path"] = _as_str(val)
        elif field == 4 and wt == 2:
            out["separate"] = decode_separate(val)
        elif field == 5 and wt == 2:
            out["matrix"] = decode_matrix4d(val)
        elif field == 7 and wt == 0:
            out["timestamp"] = val
    return out


def decode_transform_broadcast(buf: bytes) -> dict:
    out = {"client_id": "", "update": None}
    for field, wt, val in _iter_fields(buf):
        if field == 1 and wt == 2:
            out["client_id"] = _as_str(val)
        elif field == 2 and wt == 2:
            out["update"] = decode_transform_update(val)
    return out


def decode_handshake(buf: bytes) -> dict:
    out = {"session_id": "", "session_layer": "", "usd_path": "", "usd_uri": ""}
    for field, wt, val in _iter_fields(buf):
        if wt != 2:
            continue
        if field == 1:
            out["session_id"] = _as_str(val)
        elif field == 2:
            out["session_layer"] = _as_str(val)
        elif field == 3:
            out["usd_path"] = _as_str(val)
        elif field == 4:
            out["usd_uri"] = _as_str(val)
    return out


def decode_ack(buf: bytes) -> dict:
    out = {"ref_id": "", "ok": False, "error": ""}
    for field, wt, val in _iter_fields(buf):
        if field == 1 and wt == 2:
            out["ref_id"] = _as_str(val)
        elif field == 2 and wt == 0:
            out["ok"] = bool(val)
        elif field == 3 and wt == 2:
            out["error"] = _as_str(val)
    return out


def decode_error(buf: bytes) -> dict:
    out = {"code": "", "message": ""}
    for field, wt, val in _iter_fields(buf):
        if field == 1 and wt == 2:
            out["code"] = _as_str(val)
        elif field == 2 and wt == 2:
            out["message"] = _as_str(val)
    return out


def decode_base_message(buf: bytes) -> dict:
    """Returns { 'session_id': str, 'kind': one of
       handshake|xform_update|xform_broadcast|ack|error|unknown, 'payload': dict }"""
    result = {"session_id": "", "kind": "unknown", "payload": None}
    for field, wt, val in _iter_fields(buf):
        if field == 1 and wt == 2:
            result["session_id"] = _as_str(val)
        elif field == 2 and wt == 2:
            result["kind"] = "handshake"
            result["payload"] = decode_handshake(val)
        elif field == 3 and wt == 2:
            result["kind"] = "xform_update"
            result["payload"] = decode_transform_update(val)
        elif field == 4 and wt == 2:
            result["kind"] = "xform_broadcast"
            result["payload"] = decode_transform_broadcast(val)
        elif field == 5 and wt == 2:
            result["kind"] = "ack"
            result["payload"] = decode_ack(val)
        elif field == 6 and wt == 2:
            result["kind"] = "error"
            result["payload"] = decode_error(val)
    return result
