# Shared Task Coordination Service (STCS)
## Project Overview and Protocol Specification

**Course:** CIS 527 - Computer Networks  
**Student:** Sai Praneeth Bhattu  
**Coverage:** Phase 1 design and Phase 2 protocol

## 1. Project Overview

The Shared Task Coordination Service (STCS) is a TCP client-server application for small teams that need to manage a shared task list. Users can create tasks, view them, claim work, release tasks, mark tasks complete, and delete tasks.

The main goal is to prevent two users from claiming the same open task. The server keeps the shared task list and decides whether each operation is allowed.

## 2. Development History

Phase 1 established the project idea, client-server design, task operations, validation rules, and initial tests.

The original Phase 1 design used a 4-byte length prefix for TCP messages and described a Python file layout. These were later replaced. The Phase 2 protocol described in this document uses newline-delimited JSON. The Phase 3 implementation is written in C++ and its final details are documented in the Phase 3 report.

The current protocol described below is the Phase 2 protocol, STCS v1. It is the basis for the Phase 3 implementation.

## 3. Basic Architecture

The application has three main parts:

| Part | Responsibility |
| --- | --- |
| Client | Accepts user commands, sends requests, and displays responses. |
| Server | Accepts TCP connections, validates requests, runs commands, and sends responses. |
| Task Manager | Stores the shared task list and checks task state before changing it. |

The server also maintains session information and writes activity to a log.

## 4. Message Framing

TCP sends a stream of bytes. It does not preserve application message boundaries. STCS uses one JSON object per line to identify complete messages.

- Messages use UTF-8 without a byte-order mark.
- Each message ends with one newline (`\\n`).
- A message must not contain a raw newline inside its JSON text.
- The receiver keeps incomplete data until the newline arrives.
- If several complete lines arrive together, the receiver processes them in order.
- The sender continues sending until all message bytes have been written.

### Message limits

| Property | Limit or rule |
| --- | --- |
| Maximum request line | 8192 bytes, enforced by the server |
| Maximum response accepted by client | 1 MiB |
| Receive chunk | 4096 bytes |
| Request ID | 1 to 64 bytes |
| JSON nesting | Maximum 32 levels |
| Timestamp format | ISO-8601 UTC |

A request line larger than 8192 bytes receives one `MESSAGE_TOO_LARGE` response. The rest of that line is discarded through the next newline, after which normal message processing resumes. The client can accept larger responses, such as a long task list, up to 1 MiB.

## 5. Message Format

Every request contains these fields:

| Field | Type | Description |
| --- | --- | --- |
| `version` | Integer | Protocol version. Currently `1`. |
| `type` | String | Must be `REQUEST`. |
| `request_id` | String | Client-generated ID, unique within the session. |
| `command` | String | Command name. |
| `payload` | Object | Command data. Use `{}` when no data is needed. |

The server responds with `type: RESPONSE`, the same `version`, `request_id` and `command` when available, a `status` of `OK` or `ERROR`, and a `payload`. Error responses also contain `error_code` and a readable `message`.

### Example request

```json
{"version":1,"type":"REQUEST","request_id":"r3","command":"HELLO","payload":{"display_name":"Ana"}}
```

### Example success response

```json
{"version":1,"type":"RESPONSE","request_id":"r3","command":"HELLO","status":"OK","payload":{"session_id":"sess-0001","display_name":"Ana","server_version":1,"commands":["HELLO","HELP","LIST_TASKS","CREATE_TASK","CLAIM_TASK","RELEASE_TASK","COMPLETE_TASK","DELETE_TASK","QUIT"]}}
```

## 6. Request Validation

The server checks each request in this order and returns the first error it finds:

1. Check that the line is valid UTF-8 and contains a JSON object.
2. Check the protocol version.
3. Check that `type` is `REQUEST`.
4. Check `request_id`.
5. Check the command name.
6. Check that `payload` is a JSON object.
7. Check whether the command is allowed in the current session state.
8. Validate the command's fields.
9. Run the command and return success or an application error.

Unknown extra JSON fields are ignored. Repeated JSON keys are rejected.

## 7. Session States

| State | Allowed commands |
| --- | --- |
| `DISCONNECTED` | None. No socket is open. |
| `CONNECTED` | `HELLO`, `HELP`, `QUIT` |
| `READY` | Task commands, `HELP`, `QUIT` |
| `CLOSING` / `CLOSED` | None. The socket is closed and the session is released. |

A successful `HELLO` moves a session from `CONNECTED` to `READY`. A failed `HELLO` leaves the session in `CONNECTED`, so the client can try again. An invalid command for the current state returns `INVALID_STATE` and does not close the connection.

## 8. Task Information

Each task contains:

| Field | Meaning |
| --- | --- |
| `task_id` | Positive integer assigned by the server. IDs are not reused. |
| `title` | Task description, 1 to 120 Unicode code points after trimming ASCII whitespace. |
| `priority` | `LOW`, `MEDIUM`, or `HIGH`. Default is `MEDIUM`. |
| `status` | `OPEN`, `CLAIMED`, or `DONE`. |
| `claimed_by` | Display name of the user who claimed the task, or `null`. |
| `created_by` | Display name of the user who created the task. |
| `created_at` | Creation time in ISO-8601 UTC format. |

Task titles cannot be blank or contain control characters.

## 9. Supported Commands

| Command | Required data | What it does |
| --- | --- | --- |
| `HELLO` | `display_name` | Registers a user and starts the session. Names are 1 to 24 characters and may contain ASCII letters, digits, spaces, `_`, `-`, and `.`. Names cannot start or end with a space. Active names must be unique; comparison is case-sensitive. |
| `HELP` | None; optional `command` | Returns the list of server commands or details for one command. |
| `LIST_TASKS` | None; optional `status` | Returns tasks in task ID order. The status filter can be `OPEN`, `CLAIMED`, or `DONE`. |
| `CREATE_TASK` | `title`; optional `priority` | Creates an `OPEN` task. The server assigns its ID and creation time. |
| `CLAIM_TASK` | `task_id` | Claims a task only when its status is `OPEN`. If it is already claimed or done, the server returns `CONFLICT`. |
| `RELEASE_TASK` | `task_id` | Changes a `CLAIMED` task back to `OPEN`. Any session may release a claimed task. |
| `COMPLETE_TASK` | `task_id` | Marks an `OPEN` or `CLAIMED` task as `DONE`. The existing `claimed_by` value is retained. |
| `DELETE_TASK` | `task_id` | Deletes an existing task, regardless of its status. The ID is not reused. |
| `QUIT` | None; optional `reason` | Ends the session and releases the display name. A reason can be up to 200 bytes and is logged only after sanitization. Claims held by the session remain. |

All task commands are available only in the `READY` state. `HELP` and `QUIT` are available in both `CONNECTED` and `READY`.

## 10. Error Codes

| Error code | Meaning |
| --- | --- |
| `INVALID_MESSAGE` | The line is empty, invalid UTF-8, invalid JSON, or not a JSON object. |
| `INVALID_VERSION` | The request uses an unsupported integer protocol version. |
| `UNKNOWN_COMMAND` | The command name is not supported. |
| `MISSING_FIELD` | A required field is missing. |
| `INVALID_FIELD` | A field has the wrong type, length, or value. |
| `NOT_FOUND` | The requested task does not exist. |
| `CONFLICT` | The task exists, but its current state does not allow the requested operation. |
| `INVALID_STATE` | The command is not allowed in the current session state. |
| `NAME_IN_USE` | Another active session already uses the display name. |
| `MESSAGE_TOO_LARGE` | The request line exceeds 8192 bytes. |
| `SESSION_ERROR` | Reserved for a session that cannot continue. It is defined but not emitted in Phase 2. |
| `SERVER_ERROR` | An unexpected server-side error occurred. Details are kept in the server log. |

The client displays useful error messages. Responses do not expose stack traces, file paths, or internal source-code details.

## 11. Concurrency and Conflict Handling

The shared task manager must protect operations that check and change task state. A claim operation checks that a task is `OPEN` and changes it to `CLAIMED` as one protected operation. This prevents two clients from successfully claiming the same task.

When two clients attempt the same claim, one can succeed. The other receives `CONFLICT`, including the current task state and owner when available. A failed operation does not change the task.

The Phase 1 design used a lock around task state changes. The Phase 3 report describes the final C++ thread-per-client design and its mutexes in more detail.

## 12. Testing Background

The Phase 1 test plan included:

- `T-02` to `T-06`: Core command behavior.
- `T-10` to `T-12`: Concurrent claim behavior.
- `T-13`: Fragmented frames tested without sockets.
- `T-14`: Multiple complete frames received together.

A raw client was also used for protocol robustness testing. The Phase 3 report contains the final test results and expanded concurrency and load testing.

## 13. Known Limitations and Future Work

The service stores tasks in memory. Tasks are lost when the server stops. The early design did not include authentication, TLS, persistent storage, replication, or production-grade access control.

Possible future improvements include persistent storage, authentication, encrypted connections, and support for larger numbers of simultaneous clients. These are future improvements, not features claimed by this specification.

---
