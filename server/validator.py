"""Validation for incoming protocol envelopes and command fields."""
from shared.protocol import COMMANDS, VERSION


def validate_envelope(env: dict) -> tuple[bool, str, str]:
    if env.get("version") != VERSION:
        return False, "BAD_VERSION", "unsupported protocol version"
    if not isinstance(env.get("request_id"), str) or not env["request_id"]:
        return False, "BAD_FIELD", "request_id is required"
    command = env.get("command")
    if command not in COMMANDS:
        return False, "UNKNOWN_COMMAND", "unknown command"
    if not isinstance(env.get("payload", {}), dict):
        return False, "BAD_FIELD", "payload must be an object"
    return True, "", ""


def require(payload: dict, *fields: str) -> None:
    for field in fields:
        if field not in payload:
            raise ValueError(f"missing field: {field}")
