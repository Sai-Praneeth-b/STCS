import socket
import threading
from server.session import Session


def serve(host: str, port: int, application, logger) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((host, port))
        server.listen()
        logger.info("listening on %s:%s", host, port)
        while True:
            client, address = server.accept()
            logger.info("connection from %s", address)
            threading.Thread(target=Session(client, application, logger).run, daemon=True).start()
