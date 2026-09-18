"""Thread-safe task state. This is the ONLY module that acquires task_lock."""
import threading
from dataclasses import dataclass, asdict


@dataclass
class Task:
    task_id: int
    title: str
    owner: str | None = None
    status: str = "OPEN"


class TaskManager:
    def __init__(self) -> None:
        self.tasks: dict[int, Task] = {}
        self.next_id = 1
        self.task_lock = threading.RLock()

    def create(self, title: str) -> dict:
        with self.task_lock:
            task = Task(self.next_id, title)
            self.tasks[task.task_id] = task
            self.next_id += 1
            return asdict(task)

    def list(self) -> list[dict]:
        with self.task_lock:
            return [asdict(t) for t in self.tasks.values()]

    def get(self, task_id: int) -> dict | None:
        with self.task_lock:
            task = self.tasks.get(task_id)
            return asdict(task) if task else None

    def claim(self, task_id: int, user: str) -> dict:
        with self.task_lock:
            task = self.tasks.get(task_id)
            if not task:
                raise KeyError("NOT_FOUND")
            if task.status != "OPEN":
                raise RuntimeError("CONFLICT")
            task.owner = user
            task.status = "CLAIMED"
            return asdict(task)

    def release(self, task_id: int, user: str) -> dict:
        with self.task_lock:
            task = self.tasks.get(task_id)
            if not task:
                raise KeyError("NOT_FOUND")
            if task.owner != user:
                raise PermissionError("NOT_OWNER")
            if task.status != "CLAIMED":
                raise RuntimeError("INVALID_STATE")
            task.owner = None
            task.status = "OPEN"
            return asdict(task)

    def complete(self, task_id: int, user: str) -> dict:
        with self.task_lock:
            task = self.tasks.get(task_id)
            if not task:
                raise KeyError("NOT_FOUND")
            if task.owner != user:
                raise PermissionError("NOT_OWNER")
            if task.status != "CLAIMED":
                raise RuntimeError("INVALID_STATE")
            task.status = "COMPLETED"
            return asdict(task)
