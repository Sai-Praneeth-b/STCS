"""Command dispatch and application logic."""
from server.task_manager import TaskManager
from server.validator import require


class Application:
    def __init__(self, task_manager: TaskManager | None = None) -> None:
        self.tasks = task_manager or TaskManager()

    def handle(self, command: str, payload: dict, user: str) -> tuple[bool, object, str | None, str | None]:
        try:
            if command == "CREATE":
                require(payload, "title")
                if not isinstance(payload["title"], str) or not payload["title"].strip():
                    raise ValueError("title must be a non-empty string")
                return True, self.tasks.create(payload["title"].strip()), None, None
            if command == "LIST":
                return True, self.tasks.list(), None, None
            if command == "GET":
                require(payload, "task_id")
                item = self.tasks.get(int(payload["task_id"]))
                if item is None:
                    return False, None, "NOT_FOUND", "task not found"
                return True, item, None, None
            if command == "CLAIM":
                require(payload, "task_id")
                return True, self.tasks.claim(int(payload["task_id"]), user), None, None
            if command == "RELEASE":
                require(payload, "task_id")
                return True, self.tasks.release(int(payload["task_id"]), user), None, None
            if command == "COMPLETE":
                require(payload, "task_id")
                return True, self.tasks.complete(int(payload["task_id"]), user), None, None
            if command == "QUIT":
                return True, {"message": "bye"}, None, None
            return False, None, "UNKNOWN_COMMAND", "unknown command"
        except KeyError as exc:
            return False, None, str(exc).strip("'"), "task not found"
        except PermissionError as exc:
            return False, None, str(exc).strip("'"), "operation not permitted"
        except RuntimeError as exc:
            return False, None, str(exc).strip("'"), "invalid task state or conflict"
        except (ValueError, TypeError) as exc:
            return False, None, "BAD_FIELD", str(exc)
