import socket
from shared.framing import FrameBuffer, send_all
from shared.protocol import build_request, parse_envelope


class Connection:
    def __init__(self, host: str, port: int) -> None:
        self.sock = socket.create_connection((host, port))
        self.buffer = FrameBuffer()

    def request(self, request_id: str, command: str, payload: dict | None = None) -> dict:
        send_all(self.sock, build_request(request_id, command, payload))
        while True:
            data = self.sock.recv(4096)
            if not data:
                raise ConnectionError("server closed connection")
            messages = self.buffer.feed(data)
            if messages:
                return parse_envelope(messages[0])

    def close(self) -> None:
        self.sock.close()
