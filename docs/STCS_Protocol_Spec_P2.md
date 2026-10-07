# STCS Protocol Specification - STCS-P2

Shared Task Coordination Service, CIS 527 Phase 2. Student: Sai Praneeth Bhattu.

This is the protocol implemented in Phase 2. It is the same protocol (STCS v1) as Phase 1; the clarifications made while implementing are listed as C-1 to C-12 in section 9 of `STCS_Phase2_Report.docx`. All examples were captured from the running server (`protocol_examples.json`). Messages are sent as ONE line each.

## 1. Framing

One message = one JSON object, UTF-8, no raw newline, followed by exactly one `\n`. The receiver buffers bytes, processes nothing until `\n` arrives, handles complete lines in order and keeps a trailing fragment. No delay or sleep is used. Senders loop until every byte is sent.

## 2. Encoding and limits

| Property | Value |
| --- | --- |
| Encoding | UTF-8, no BOM |
| Delimiter | `\n` (a single trailing `\r` is stripped) |
| Max request size | 8192 bytes per line (applies to requests received by the server) |
| Max response size accepted by the client | 1 MiB (a LIST_TASKS reply can exceed 8192 bytes; C-1) |
| Receive chunk | 4096 bytes |
| Timestamps | ISO-8601 UTC, e.g. 2026-10-04T15:01:17Z |
| Request id | string, 1-64 bytes |
| JSON nesting depth | at most 32 |

An oversize line gets one MESSAGE_TOO_LARGE; bytes up to the next newline are discarded and the stream resynchronises.

## 3. Message envelope

**Request**

| Field | Type | Required | Notes |
| --- | --- | --- | --- |
| version | integer | yes | 1 |
| type | string | yes | "REQUEST" |
| request_id | string | yes | 1-64 bytes; client-generated, unique per session, echoed back unchanged |
| command | string | yes | One of the nine commands |
| payload | object | yes | {} when the command takes no arguments |

**Response:** same `version`, `type` "RESPONSE", echoed `request_id` and `command` (null when not recoverable), `status` ("OK"/"ERROR"), `payload`, and for errors `error_code` and `message`.

**Order of validation** (first failure decides the reply):

1. The line must be valid UTF-8 and a JSON object, else INVALID_MESSAGE (request_id and command null).
2. `version`: missing gives MISSING_FIELD; a non-integer gives INVALID_FIELD; an integer other than 1 gives INVALID_VERSION (payload lists supported_versions).
3. `type` must be the string "REQUEST" (missing gives MISSING_FIELD, other values give INVALID_FIELD).
4. `request_id`: missing gives MISSING_FIELD; anything other than a string of 1-64 bytes gives INVALID_FIELD.
5. `command`: missing gives MISSING_FIELD; a non-string gives INVALID_FIELD; a name outside the table gives UNKNOWN_COMMAND.
6. `payload` must be a JSON object.
7. Session-state check: a command that is not legal in the current state gives INVALID_STATE.
8. Payload field validation (MISSING_FIELD / INVALID_FIELD).
9. Execution (NOT_FOUND / CONFLICT, or success).

Unknown extra fields are ignored; duplicate JSON keys are rejected.

## 4. Session states

| State | Legal commands |
| --- | --- |
| DISCONNECTED | none (no socket) |
| CONNECTED | HELLO, HELP, QUIT; anything else returns INVALID_STATE |
| READY | all task commands, HELP, QUIT (a second HELLO returns INVALID_STATE) |
| CLOSING, CLOSED | none; the socket is closed and the session released |

A failed HELLO leaves the session in CONNECTED. INVALID_STATE never closes the connection.

## 5. Commands

Task entity: task_id (integer >= 1), title (string), priority ("LOW" | "MEDIUM" | "HIGH"), status ("OPEN" | "CLAIMED" | "DONE"), claimed_by (string or null), created_by (string), created_at (ISO-8601 UTC string).

### 5.1 HELLO

| Item | Description |
| --- | --- |
| Purpose | Starts the session: registers the client's display name and moves the session from CONNECTED to READY. |
| State | CONNECTED only (a second HELLO returns INVALID_STATE). |
| Sender | Client (automatically, as the first request after connecting). |
| Receiver | Server; the session record is created at accept() and completed here. |
| Required fields | `display_name` |
| Optional fields | none |
| Types | `display_name`: string. |
| Validation | 1-24 characters; only ASCII letters, digits, space, `_`, `-`, `.`; no leading or trailing space; must not equal (exact, case-sensitive match) the name of another active session. |
| Behavior | On success the session state becomes READY and the name is reserved until the session is released. A failed HELLO leaves the session in CONNECTED so the client can retry with another name. |
| Success response | `session_id` (string, e.g. "sess-0001"), `display_name`, `server_version` (1), `commands` (array of the 9 command names). |
| Errors | MISSING_FIELD, INVALID_FIELD, NAME_IN_USE, INVALID_STATE (also INVALID_VERSION, INVALID_MESSAGE from the envelope). |

**Example request**

```json
{"version":1,"type":"REQUEST","request_id":"r3","command":"HELLO","payload":{"display_name":"Ana"}}
```

**Example success response**

```json
{"version":1,"type":"RESPONSE","request_id":"r3","command":"HELLO","status":"OK","payload":{"session_id":"sess-0001","display_name":"Ana","server_version":1,"commands":["HELLO","HELP","LIST_TASKS","CREATE_TASK","CLAIM_TASK","RELEASE_TASK","COMPLETE_TASK","DELETE_TASK","QUIT"]}}
```

**Example error response(s)**

```json
{"version":1,"type":"REQUEST","request_id":"r2","command":"HELLO","payload":{"display_name":"bad!"}}
{"version":1,"type":"RESPONSE","request_id":"r2","command":"HELLO","status":"ERROR","error_code":"INVALID_FIELD","message":"Field 'display_name' may contain only letters, digits, space, '_', '-' and '.'.","payload":{"field":"display_name"}}
```

```json
{"version":1,"type":"REQUEST","request_id":"r4","command":"HELLO","payload":{"display_name":"Ana"}}
{"version":1,"type":"RESPONSE","request_id":"r4","command":"HELLO","status":"ERROR","error_code":"INVALID_STATE","message":"HELLO has already been completed for this session.","payload":{"state":"READY"}}
```

### 5.2 HELP

| Item | Description |
| --- | --- |
| Purpose | Returns the server's command list, or the entry for one command. |
| State | CONNECTED or READY. |
| Sender | Client (`help server [COMMAND]`). |
| Receiver | Server dispatcher. |
| Required fields | none |
| Optional fields | `command` |
| Types | `command`: string. |
| Validation | If present, `command` must be one of the nine command names (exact upper-case match). |
| Behavior | Read-only. Without `command` the reply lists all nine commands in protocol order. |
| Success response | `commands`: array of objects with `name`, `summary`, `required_fields`. |
| Errors | INVALID_FIELD (`command` not a string or not a known command name). |

**Example request**

```json
{"version":1,"type":"REQUEST","request_id":"r5","command":"HELP","payload":{"command":"CLAIM_TASK"}}
```

**Example success response**

```json
{"version":1,"type":"RESPONSE","request_id":"r5","command":"HELP","status":"OK","payload":{"commands":[{"name":"CLAIM_TASK","summary":"Take ownership of an OPEN task.","required_fields":["task_id"]}]}}
```

**Example error response(s)**

```json
{"version":1,"type":"REQUEST","request_id":"r6","command":"HELP","payload":{"command":"ARCHIVE"}}
{"version":1,"type":"RESPONSE","request_id":"r6","command":"HELP","status":"ERROR","error_code":"INVALID_FIELD","message":"Unknown command name 'ARCHIVE'.","payload":{"field":"command"}}
```

### 5.3 LIST_TASKS

| Item | Description |
| --- | --- |
| Purpose | Returns the shared task list; the way every client sees the current server state. |
| State | READY. |
| Sender | Client (`list [open\|claimed\|done]`). |
| Receiver | Server dispatcher, task manager. |
| Required fields | none |
| Optional fields | `status` |
| Types | `status`: string. |
| Validation | If present, `status` must be exactly OPEN, CLAIMED or DONE. |
| Behavior | Read-only; tasks are returned in ascending `task_id` order. The reply line can exceed 8192 bytes for large lists (see clarification C-1). |
| Success response | `tasks` (array of Task), `count` (integer). |
| Errors | INVALID_FIELD, INVALID_STATE. |

**Example request**

```json
{"version":1,"type":"REQUEST","request_id":"r10","command":"LIST_TASKS","payload":{"status":"OPEN"}}
```

**Example success response**

```json
{"version":1,"type":"RESPONSE","request_id":"r10","command":"LIST_TASKS","status":"OK","payload":{"tasks":[{"task_id":1,"title":"Write methodology section","priority":"HIGH","status":"OPEN","claimed_by":null,"created_by":"Ana","created_at":"2026-10-04T20:11:25Z"}],"count":1}}
```

**Example error response(s)**

```json
{"version":1,"type":"REQUEST","request_id":"r11","command":"LIST_TASKS","payload":{"status":"INVALID"}}
{"version":1,"type":"RESPONSE","request_id":"r11","command":"LIST_TASKS","status":"ERROR","error_code":"INVALID_FIELD","message":"Field 'status' must be OPEN, CLAIMED, or DONE.","payload":{"field":"status"}}
```

### 5.4 CREATE_TASK

| Item | Description |
| --- | --- |
| Purpose | Adds a task to the shared list. |
| State | READY. |
| Sender | Client (`add [title]`). |
| Receiver | Server dispatcher, task manager. |
| Required fields | `title` |
| Optional fields | `priority` (default MEDIUM) |
| Types | `title`: string. `priority`: string. |
| Validation | `title`: after trimming ASCII whitespace, 1-120 Unicode code points, not blank, no control characters (U+0000-001F, U+007F, U+0080-009F). `priority`: exactly LOW, MEDIUM or HIGH. |
| Behavior | The server assigns `task_id` from a counter (never reused), sets status OPEN, claimed_by null, created_by = the session's display name and created_at = server UTC time. The trimmed title is stored. |
| Success response | `task` (full Task object). |
| Errors | MISSING_FIELD, INVALID_FIELD, INVALID_STATE. |

**Example request**

```json
{"version":1,"type":"REQUEST","request_id":"r7","command":"CREATE_TASK","payload":{"title":"Write methodology section","priority":"HIGH"}}
```

**Example success response**

```json
{"version":1,"type":"RESPONSE","request_id":"r7","command":"CREATE_TASK","status":"OK","payload":{"task":{"task_id":1,"title":"Write methodology section","priority":"HIGH","status":"OPEN","claimed_by":null,"created_by":"Ana","created_at":"2026-10-04T20:11:25Z"}}}
```

**Example error response(s)**

```json
{"version":1,"type":"REQUEST","request_id":"r8","command":"CREATE_TASK","payload":{"priority":"HIGH"}}
{"version":1,"type":"RESPONSE","request_id":"r8","command":"CREATE_TASK","status":"ERROR","error_code":"MISSING_FIELD","message":"Field 'title' is required.","payload":{"field":"title"}}
```

```json
{"version":1,"type":"REQUEST","request_id":"r9","command":"CREATE_TASK","payload":{"title":"x","priority":"URGENT"}}
{"version":1,"type":"RESPONSE","request_id":"r9","command":"CREATE_TASK","status":"ERROR","error_code":"INVALID_FIELD","message":"Field 'priority' must be LOW, MEDIUM, or HIGH.","payload":{"field":"priority"}}
```

### 5.5 CLAIM_TASK

| Item | Description |
| --- | --- |
| Purpose | Takes visible ownership of an OPEN task so that nobody duplicates the work (the distinctive feature). |
| State | READY. |
| Sender | Client (`claim <id>`). |
| Receiver | Server dispatcher, task manager. |
| Required fields | `task_id` |
| Optional fields | none |
| Types | `task_id`: JSON integer. |
| Validation | A JSON integer >= 1. Strings (even "7"), floats, booleans and null are rejected with INVALID_FIELD. The task must exist; its status must be exactly OPEN. |
| Behavior | In one check-then-act step the task manager re-reads the task, checks OPEN and sets status CLAIMED and claimed_by = the session's display name. A task that is not OPEN is left unchanged. |
| Success response | `task` (full Task object). |
| Errors | MISSING_FIELD, INVALID_FIELD, NOT_FOUND, CONFLICT (payload: `task_id`, current `status`, and `claimed_by` when owned), INVALID_STATE. |

**Example request**

```json
{"version":1,"type":"REQUEST","request_id":"r12","command":"CLAIM_TASK","payload":{"task_id":1}}
```

**Example success response**

```json
{"version":1,"type":"RESPONSE","request_id":"r12","command":"CLAIM_TASK","status":"OK","payload":{"task":{"task_id":1,"title":"Write methodology section","priority":"HIGH","status":"CLAIMED","claimed_by":"Ana","created_by":"Ana","created_at":"2026-10-04T20:11:25Z"}}}
```

**Example error response(s)**

```json
{"version":1,"type":"REQUEST","request_id":"r13","command":"CLAIM_TASK","payload":{"task_id":1}}
{"version":1,"type":"RESPONSE","request_id":"r13","command":"CLAIM_TASK","status":"ERROR","error_code":"CONFLICT","message":"Task 1 is already claimed by Ana.","payload":{"task_id":1,"status":"CLAIMED","claimed_by":"Ana"}}
```

```json
{"version":1,"type":"REQUEST","request_id":"r14","command":"CLAIM_TASK","payload":{"task_id":9999}}
{"version":1,"type":"RESPONSE","request_id":"r14","command":"CLAIM_TASK","status":"ERROR","error_code":"NOT_FOUND","message":"No task exists with id 9999.","payload":{"task_id":9999}}
```

```json
{"version":1,"type":"REQUEST","request_id":"r15","command":"CLAIM_TASK","payload":{"task_id":"abc"}}
{"version":1,"type":"RESPONSE","request_id":"r15","command":"CLAIM_TASK","status":"ERROR","error_code":"INVALID_FIELD","message":"Field 'task_id' must be an integer greater than or equal to 1.","payload":{"field":"task_id"}}
```

```json
{"version":1,"type":"REQUEST","request_id":"r16","command":"CLAIM_TASK","payload":{}}
{"version":1,"type":"RESPONSE","request_id":"r16","command":"CLAIM_TASK","status":"ERROR","error_code":"MISSING_FIELD","message":"Field 'task_id' is required.","payload":{"field":"task_id"}}
```

### 5.6 RELEASE_TASK

| Item | Description |
| --- | --- |
| Purpose | Returns a CLAIMED task to OPEN so that someone else can take it. |
| State | READY. |
| Sender | Client (`release <id>`). |
| Receiver | Server dispatcher, task manager. |
| Required fields | `task_id` |
| Optional fields | none |
| Types | `task_id`: JSON integer. |
| Validation | JSON integer >= 1; the task must exist and be CLAIMED. Any session may release any claimed task (Phase 1 open question Q2); the releasing user is the `released_by` value. |
| Behavior | Sets status OPEN and claimed_by null. |
| Success response | `task` (full Task object), `released_by` (string). |
| Errors | MISSING_FIELD, INVALID_FIELD, NOT_FOUND, CONFLICT (not claimed), INVALID_STATE. |

**Example request**

```json
{"version":1,"type":"REQUEST","request_id":"r17","command":"RELEASE_TASK","payload":{"task_id":1}}
```

**Example success response**

```json
{"version":1,"type":"RESPONSE","request_id":"r17","command":"RELEASE_TASK","status":"OK","payload":{"task":{"task_id":1,"title":"Write methodology section","priority":"HIGH","status":"OPEN","claimed_by":null,"created_by":"Ana","created_at":"2026-10-04T20:11:25Z"},"released_by":"Ana"}}
```

**Example error response(s)**

```json
{"version":1,"type":"REQUEST","request_id":"r18","command":"RELEASE_TASK","payload":{"task_id":1}}
{"version":1,"type":"RESPONSE","request_id":"r18","command":"RELEASE_TASK","status":"ERROR","error_code":"CONFLICT","message":"Task 1 is not claimed, so it cannot be released.","payload":{"task_id":1,"status":"OPEN"}}
```

### 5.7 COMPLETE_TASK

| Item | Description |
| --- | --- |
| Purpose | Marks work finished so the remaining-work view stays accurate. |
| State | READY. |
| Sender | Client (`done <id>`). |
| Receiver | Server dispatcher, task manager. |
| Required fields | `task_id` |
| Optional fields | none |
| Types | `task_id`: JSON integer. |
| Validation | JSON integer >= 1; the task must exist and be OPEN or CLAIMED. |
| Behavior | Sets status DONE and keeps `claimed_by` as the record of who did the work (a task completed directly from OPEN keeps claimed_by null). |
| Success response | `task` (full Task object). |
| Errors | MISSING_FIELD, INVALID_FIELD, NOT_FOUND, CONFLICT (already DONE), INVALID_STATE. |

**Example request**

```json
{"version":1,"type":"REQUEST","request_id":"r20","command":"COMPLETE_TASK","payload":{"task_id":1}}
```

**Example success response**

```json
{"version":1,"type":"RESPONSE","request_id":"r20","command":"COMPLETE_TASK","status":"OK","payload":{"task":{"task_id":1,"title":"Write methodology section","priority":"HIGH","status":"DONE","claimed_by":"Ana","created_by":"Ana","created_at":"2026-10-04T20:11:25Z"}}}
```

**Example error response(s)**

```json
{"version":1,"type":"REQUEST","request_id":"r21","command":"COMPLETE_TASK","payload":{"task_id":1}}
{"version":1,"type":"RESPONSE","request_id":"r21","command":"COMPLETE_TASK","status":"ERROR","error_code":"CONFLICT","message":"Task 1 is already marked done.","payload":{"task_id":1,"status":"DONE"}}
```

### 5.8 DELETE_TASK

| Item | Description |
| --- | --- |
| Purpose | Permanently removes an obsolete task so the list stays trustworthy. |
| State | READY. |
| Sender | Client (`delete <id>`). |
| Receiver | Server dispatcher, task manager. |
| Required fields | `task_id` |
| Optional fields | none |
| Types | `task_id`: JSON integer. |
| Validation | JSON integer >= 1; the task must exist. A task in any status may be deleted. |
| Behavior | Removes the record; the id is never reused. |
| Success response | `task_id` (integer), `deleted` (true). |
| Errors | MISSING_FIELD, INVALID_FIELD, NOT_FOUND, INVALID_STATE. |

**Example request**

```json
{"version":1,"type":"REQUEST","request_id":"r23","command":"DELETE_TASK","payload":{"task_id":2}}
```

**Example success response**

```json
{"version":1,"type":"RESPONSE","request_id":"r23","command":"DELETE_TASK","status":"OK","payload":{"task_id":2,"deleted":true}}
```

**Example error response(s)**

```json
{"version":1,"type":"REQUEST","request_id":"r24","command":"DELETE_TASK","payload":{"task_id":2}}
{"version":1,"type":"RESPONSE","request_id":"r24","command":"DELETE_TASK","status":"ERROR","error_code":"NOT_FOUND","message":"No task exists with id 2.","payload":{"task_id":2}}
```

### 5.9 QUIT

| Item | Description |
| --- | --- |
| Purpose | Ends the session gracefully so the server can log a clean disconnect and release resources. |
| State | CONNECTED or READY. |
| Sender | Client (`quit`, or automatically at end of input). |
| Receiver | Server dispatcher; the server loop closes the socket. |
| Required fields | none |
| Optional fields | `reason` |
| Types | `reason`: string. |
| Validation | If present, `reason` is a string of at most 200 bytes. It is logged only (sanitised), never stored. |
| Behavior | The server sends the response, moves the session to CLOSING, closes the socket, releases the session (freeing the display name), logs "disconnected gracefully" and returns to listening. Claims held by the session remain (Q2). A malformed QUIT gets an error and does NOT close the session. |
| Success response | `message` ("Session closed."), `session_id`. |
| Errors | INVALID_FIELD (bad `reason`). Envelope errors as for every command. |

**Example request**

```json
{"version":1,"type":"REQUEST","request_id":"r31","command":"QUIT","payload":{}}
```

**Example success response**

```json
{"version":1,"type":"RESPONSE","request_id":"r31","command":"QUIT","status":"OK","payload":{"message":"Session closed.","session_id":"sess-0001"}}
```

**Example error response(s)**

```json
{"version":1,"type":"REQUEST","request_id":"r30","command":"QUIT","payload":{"reason":5}}
{"version":1,"type":"RESPONSE","request_id":"r30","command":"QUIT","status":"ERROR","error_code":"INVALID_FIELD","message":"Field 'reason' must be a string of at most 200 bytes.","payload":{"field":"reason"}}
```

## 6. Error codes

| Code | When generated | Phase 2 implementation | Client behavior |
| --- | --- | --- | --- |
| INVALID_MESSAGE | Line is not valid UTF-8 JSON, not a JSON object, or is empty. | Implemented. request_id and command are null. | Report that the message could not be understood; do not resend unchanged. |
| INVALID_VERSION | `version` is an integer other than 1. | Implemented. Payload: field, supported_versions [1]. | Show the supported versions. |
| UNKNOWN_COMMAND | `command` is not one of the nine commands. | Implemented. Checked before the payload; payload {"field":"command"}. | Show the command; suggest help; session continues. |
| MISSING_FIELD | An envelope or payload field is absent. | Implemented. Payload {"field": name}. | Prompt for the named field. |
| INVALID_FIELD | A field has the wrong type, length or value (also a non-integer `version`). | Implemented. Payload {"field": name}. | Explain the constraint and re-prompt. |
| NOT_FOUND | The task_id does not exist. | Implemented. Payload {"task_id": n}. | Say the task does not exist; suggest list. |
| CONFLICT | Well-formed request that conflicts with current state (claim a non-OPEN task, release an unclaimed task, complete a DONE task). | Implemented. Payload: task_id, status, claimed_by when owned. The task is unchanged. | Show the owner/status; the normal outcome of a lost race. |
| INVALID_STATE | Command not legal in the session state (e.g. before HELLO; second HELLO). | Implemented. Payload {"state": ...}. Never closes the connection. | Send HELLO first. |
| NAME_IN_USE | Display name held by another active session. | Implemented and unit-tested; cannot occur with a single client, so integration testing is deferred to Phase 3. | Prompt for a different name and retry. |
| MESSAGE_TOO_LARGE | A request line exceeded 8192 bytes. | Implemented. One error per oversize line; the stream resynchronises at the next newline. | Shorten the input. |
| SESSION_ERROR | The session cannot continue. | Defined, never emitted in Phase 2 (see C-8). | Report and close. |
| SERVER_ERROR | Unexpected server-side failure on a valid request. | Implemented as the dispatcher catch-all (unit test U-D7); details go to the log only. | Report that the request could not be completed. |

No error message contains a stack trace, file path, line number or internal identifier.
