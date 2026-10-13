"""Raw protocol client used to send arbitrary bytes without framing helpers."""
import argparse
import socket

p = argparse.ArgumentParser()
p.add_argument("--host", default="127.0.0.1")
p.add_argument("--port", type=int, default=5000)
p.add_argument("data", help="raw bytes represented as UTF-8 text")
a = p.parse_args()
with socket.create_connection((a.host, a.port)) as s:
    s.sendall(a.data.encode())
    print(s.recv(4096))
