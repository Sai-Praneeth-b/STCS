"""Application-layer protocol definitions and envelope helpers."""
import json
from typing import Any

VERSION = 1

COMMANDS = {"CREATE", "LIST", "GET", "CLAIM", "RELEASE", "COMPLETE", "QUIT"}

ERROR_CODES = {
    "BAD_JSON", "BAD_ENVELOPE", "BAD_VERSION", "UNKNOWN_COMMAND", "BAD_FIELD",
    "NOT_FOUND", "ALREADY_EXISTS", "CONFLICT", "NOT_OWNER", "INVALID_STATE",
    "SERVER_ERROR"
}


def build_request(request_id: str, command: str, payload: dict[str, Any] | None = None) -> bytes:
    envelope = {
        "version": VERSION,
        "request_id": request_id,
        "command": command,
        "payload": payload or {},
    }
    return json.dumps(envelope, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def build_response(request_id: str, data: Any = None, *, ok: bool = True,
                   error_code: str | None = None, message: str | None = None) -> bytes:
    envelope: dict[str, Any] = {"version": VERSION, "request_id": request_id, "ok": ok}
    if ok:
        envelope["data"] = data
    else:
        envelope["error"] = {"code": error_code or "SERVER_ERROR", "message": message or ""}
    return json.dumps(envelope, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def parse_envelope(data: bytes) -> dict[str, Any]:
    try:
        value = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError("BAD_JSON") from exc
    if not isinstance(value, dict):
        raise ValueError("BAD_ENVELOPE")
    return value
