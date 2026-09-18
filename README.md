# Shared Task Coordination Service (STCS)

**Course:** CIS 527 – Computer Networks  
**Student:** Sai Praneeth Bhattu  
**Phase:** 1 – Curiosity Report and Detailed Design  
**Due Date:** 09/18/2026

---

## Project Overview

STCS is a TCP client-server application that lets small student groups share a task list in real time. Any group member can add tasks, claim ownership of one, mark it complete, or delete it. A single server holds the authoritative list so every client always sees the same state.

This repository contains the Phase 1 design documents only. No source code exists yet; implementation begins in Phase 2.

---

## Repository Structure

```
stcs-phase1/
├── README.md                          ← this file
└── docs/
    └── Shared_Task_Coordination_Service_-Phase1-Report.pdf
```

---

## Phase 1 Deliverables (all in docs/)

| Document section | Location |
|---|---|
| Curiosity Report (problem, questions, users, value proposition, risks) | Report §1 |
| Project Proposal | Report §2 |
| Functional Requirements (FR-01 – FR-28) | Report §3 |
| Nonfunctional Requirements (NFR-01 – NFR-16) | Report §4 |
| Architecture Diagram | Report §5 |
| Sequence Diagrams (normal flow + concurrent CLAIM conflict) | Report §6 |
| Protocol Specification – STCS v1 | Report §7 |
| Data Design (TaskStore, ClientSession, SessionRegistry) | Report §8 |
| Initial Test Plan (T-01 – T-14) | Report §9 |
| Individual Contribution Plan | Report §10 |

---

## Application Summary

**Problem:** Student groups coordinate tasks through chat and shared documents, producing duplicated effort and missed work because there is no authoritative record of who is doing what.

**Solution:** A newline-delimited JSON protocol over TCP. One server holds the task list for the lifetime of the server process. Clients connect, identify themselves with a display name (HELLO), and issue commands (CREATE\_TASK, LIST\_TASKS, CLAIM\_TASK, RELEASE\_TASK, COMPLETE\_TASK, DELETE\_TASK, QUIT).

**Distinctive feature:** CLAIM\_TASK – a client takes explicit ownership of an OPEN task. The server serialises concurrent claims inside a single critical section so exactly one client wins and the other receives a CONFLICT response with the current owner's name.

---

## Protocol at a Glance

| Property | Value |
|---|---|
| Transport | TCP |
| Framing | Newline-delimited (`\n`); one JSON object per line |
| Encoding | UTF-8, no BOM |
| Max message size | 8 192 bytes |
| Message direction | All REQUEST messages: Client → Server; all RESPONSE messages: Server → Client |
| Protocol version | 1 |
| Push notifications | None in v1 (pull-on-demand via LIST\_TASKS) |

**Commands:** HELLO · HELP · LIST\_TASKS · CREATE\_TASK · CLAIM\_TASK · RELEASE\_TASK · COMPLETE\_TASK · DELETE\_TASK · QUIT

---

## No Build or Execution Instructions (Phase 1)

No source code is submitted for Phase 1. Build instructions, server startup, client startup, and command-line arguments will be provided in the Phase 2 README once implementation is complete.

---

## Individual Project Note

This project is completed by a single student with instructor approval of the individual arrangement. All design, documentation, and future implementation work is the sole responsibility of Sai Praneeth Bhattu.

---

## Known Limitations (Phase 1 Design)

- Tasks are stored in memory only; a server restart loses all task data (see open question Q1 and FR-27, marked Future Work).
- No authentication: display names are identifiers, not credentials (NFR-15).
- No server-push notifications in v1; clients poll via LIST\_TASKS (FR-28, marked Future Work).
- Concurrency (Phase 3) is designed but not yet implemented.

---

## Attribution

No external libraries are used in Phase 1. Protocol design, requirements, diagrams, and test plan are original work by Sai Praneeth Bhattu.
