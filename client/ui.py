import itertools


def run_ui(connection, user: str) -> None:
    ids = itertools.count(1)
    print("STCS client. Type help for commands; quit to exit.")
    while True:
        try:
            line = input("stcs> ").strip()
        except EOFError:
            break
        if not line:
            continue
        parts = line.split(maxsplit=2)
        command = parts[0].upper()
        if command == "HELP":
            print("create <title> | list | get <id> | claim <id> | release <id> | complete <id> | quit")
            continue
        payload = {"user": user}
        if command == "CREATE":
            if len(parts) < 2: print("usage: create <title>"); continue
            payload["title"] = " ".join(parts[1:])
        elif command in {"GET", "CLAIM", "RELEASE", "COMPLETE"}:
            if len(parts) != 2: print(f"usage: {command.lower()} <id>"); continue
            payload["task_id"] = int(parts[1])
        elif command == "LIST":
            pass
        elif command == "QUIT":
            response = connection.request(str(next(ids)), command, payload)
            print(response)
            break
        else:
            print("unknown command; type help")
            continue
        response = connection.request(str(next(ids)), command, payload)
        print(response.get("data") if response.get("ok") else response.get("error"))
