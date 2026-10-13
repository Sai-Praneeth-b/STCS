import json
import socket
from shared.framing import FrameBuffer, send_all
from shared.protocol import build_response, parse_envelope
from server.validator import validate_envelope


class Session:
    def __init__(self, sock: socket.socket, application, logger) -> None:
        self.sock = sock
        self.application = application
        self.logger = logger

    def run(self) -> None:
        buffer = FrameBuffer()
        user = "anonymous"
        try:
            while True:
                data = self.sock.recv(4096)
                if not data:
                    break
                for raw in buffer.feed(data):
                    try:
                        env = parse_envelope(raw)
                        valid, code, message = validate_envelope(env)
                        request_id = env.get("request_id", "")
                        if not valid:
                            send_all(self.sock, build_response(request_id, ok=False, error_code=code, message=message))
                            continue
                        payload = env.get("payload", {})
                        user = payload.get("user", user)
                        ok, result, error_code, message = self.application.handle(env["command"], payload, user)
                        send_all(self.sock, build_response(request_id, result, ok=ok, error_code=error_code, message=message))
                        if env["command"] == "QUIT":
                            return
                    except Exception as exc:
                        self.logger.exception("session error: %s", exc)
                        send_all(self.sock, build_response("", ok=False, error_code="SERVER_ERROR", message="internal server error"))
        finally:
            self.sock.close()
