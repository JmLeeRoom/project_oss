---
name: mock-api-engine
description: >-
  Runs and controls the standalone Mock API Server for Cogito++ Web Dashboard.
  Implements §12-5 API specifications (REST 15 endpoints, SSE stream, W6 WebSocket rejection) to enable early frontend development
  and human-in-the-loop approval testing without requiring the C++ core engine.
---

# Mock API Engine Skill

This skill allows Codex to implement and run a standalone mock backend in `tools/mock_server` against the Gemini-owned API contract. It mirrors the expected behavior of `cogito_abi` and the embedded web host.

## Supported Endpoints & Scenarios (§12-5)

- **Command Endpoints (POST — 202 Accepted, command_id required)**:
  - `POST /api/turn`: Initiate a turn asynchronously (results via SSE only).
  - `POST /api/approve`: Approve pending action (`{approval_id, action_digest_hex, nonce}`).
  - `POST /api/reject`: Reject pending action (`{approval_id, action_digest_hex, nonce, reason}`).
  - `POST /api/cancel`: Explicit operator cancellation.
  - `POST /api/indeterminate/ack`: Clear indeterminate lockdown.
  - `POST /api/finalize/retry`: Retry finalize.
  - `POST /api/session/seal`: Seal session.

- **Query Endpoints (GET)**:
  - `GET /api/state`: Current FSM state, epoch, monotonic ns, write count.
  - `GET /api/approvals/pending`: Active pending approval queue (up to 64).
  - `GET /api/tools`: Tool registry descriptors and metadata.
  - `GET /api/budget`: Token and execution budget statistics.
  - `GET /api/transitions`: Complete FSM transition table (19 explicit + R0~R4).
  - `GET /api/audit`: Cryptographic audit log journal (whitelist projection).
  - `GET /api/turns/{turn_id}`: Stored turn outcome.
  - `GET /api/events`: Server-Sent Events (SSE) unidirectional event stream.

- **SSE Events (`GET /api/events`)**:
  - `state_change`: Real-time 10-state FSM transitions (`Idle`, `Infer`, `Propose`, `Gate`, `AwaitApproval`, `Execute`, `Observe`, `Done`, `Failed`, `Cancelled`).
  - `pending_approval`: §8-5 payload with digest, nonce, before/requested parameters, untrusted text.
  - `audit_event`: Structured audit hash-chain entry with seq id.
  - `verdict`: Approval verdict resolution (`allowed`, `rejected`, `denied`, `approval_expired`, `approval_reentry_exceeded`).
  - `agent_message`: Model thinking and conversation text stream.

## Usage Workflow

1. Start mock server:
   ```bash
   cd tools/mock_server && npm start
   ```
2. Trigger test scenario via `POST /api/turn` (e.g. "Inspection threshold change -> Ask" or "Unauthorized tool call -> Deny").
3. Connect frontend `tools/web_dashboard` to `http://127.0.0.1:8080/`.

