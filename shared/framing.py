"""Length-prefixed TCP framing helpers."""
import socket
import struct

HEADER_SIZE = 4
MAX_MESSAGE = 1024 * 1024


def encode_frame(message: bytes) -> bytes:
    if len(message) > MAX_MESSAGE:
        raise ValueError("message too large")
    return struct.pack(">I", len(message)) + message


def send_all(sock: socket.socket, message: bytes) -> None:
    sock.sendall(encode_frame(message))


class FrameBuffer:
    def __init__(self) -> None:
        self.buffer = bytearray()

    def feed(self, data: bytes) -> list[bytes]:
        self.buffer.extend(data)
        messages: list[bytes] = []
        while len(self.buffer) >= HEADER_SIZE:
            size = struct.unpack(">I", self.buffer[:HEADER_SIZE])[0]
            if size > MAX_MESSAGE:
                raise ValueError("message too large")
            if len(self.buffer) < HEADER_SIZE + size:
                break
            start = HEADER_SIZE
            end = start + size
            messages.append(bytes(self.buffer[start:end]))
            del self.buffer[:end]
        return messages


def recv_message(sock: socket.socket, buffer: FrameBuffer | None = None) -> bytes | None:
    buf = buffer or FrameBuffer()
    while True:
        messages = buf.feed(sock.recv(4096))
        if messages:
            return messages[0]
        if not buf.buffer:
            return None
