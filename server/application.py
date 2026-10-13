import argparse
from server.application import Application
from server.listener import serve
from server.logger import configure


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--log", default="logs/stcs.log")
    args = parser.parse_args()
    logger = configure(args.log)
    serve("0.0.0.0", args.port, Application(), logger)


if __name__ == "__main__":
    main()
