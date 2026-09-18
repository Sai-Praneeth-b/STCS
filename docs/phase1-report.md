# Shared Task Coordination Service — Phase 1 Report

## 1. Scope

Phase 1 establishes the application-layer protocol, TCP framing, client/server separation, command handling, validation, thread-safe task management, logging, and automated tests.

## 2. Architecture

The client uses `client/connection.py` for TCP I/O and `client/ui.py` for the command interface. The server accepts connections in `listener.py`, assigns each connection to a `Session`, validates protocol messages, and delegates commands to `Application`. All shared task state is owned by `TaskManager`.

## 3. Protocol

Messages use a 4-byte big-endian length prefix followed by a UTF-8 JSON envelope. This prevents message-boundary ambiguity inherent in TCP streams. Request envelopes contain protocol version, request ID, command, and payload.

## 4. Concurrency

The task manager uses a re-entrant lock. Only `task_manager.py` acquires this lock. In particular, claim operations perform the state check and state transition while holding the same lock, preventing two concurrent clients from successfully claiming the same open task.

## 5. Tests

- T-02..T-06 cover core command behavior.
- T-10..T-12 exercise concurrent claim behavior.
- T-13 tests fragmented frames without sockets.
- T-14 tests multiple complete frames arriving together.
- `raw_client.py` supports protocol robustness testing by bypassing the normal framing client.

## 6. Limitations / Future Work

Phase 1 uses in-memory state and has no authentication, TLS, persistence, replication, or production-grade access control. Later phases can extend the command set and storage layer while retaining the framing and application boundaries.
