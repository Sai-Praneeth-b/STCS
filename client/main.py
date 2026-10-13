import argparse
from client.connection import Connection
from client.ui import run_ui


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--user", default="user1")
    args = parser.parse_args()
    connection = Connection(args.host, args.port)
    try:
        run_ui(connection, args.user)
    finally:
        connection.close()


if __name__ == "__main__":
    main()
