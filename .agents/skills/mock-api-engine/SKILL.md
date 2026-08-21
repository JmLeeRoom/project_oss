---
name: mock-api-engine
description: >-
  Runs and controls the standalone Mock API Server for Cogito++ Web Dashboard.
  Implements §12-5 API specifications (REST, WebSocket, SSE) to enable early frontend development
  and human-in-the-loop approval testing without requiring the C++ core engine.
---

# Mock API Engine Skill

This skill allows Antigravity to run a standalone mock backend in `tools/mock_server` that mirrors the exact behavior of `cogito_abi` and the embedded web host.

## Supported Endpoints & Scenarios

- **REST Endpoints (§12-5)**:
  - `POST /api/v1/session/start`: Initialize new session UUID.
  - `POST /api/v1/agent/turn`: Send user message and stream agent thinking/action proposals.
  - `POST /api/v1/approval/respond`: Receive human approval (`Approved` or `Rejected`).
  - `GET /api/v1/audit/logs`: Return structured hash-chain audit entries from mock SQLite database.
  - `GET /api/v1/tools/registry`: Return registered tool descriptors and schema metadata.

- **WebSocket Events**:
  - `fsm_state_change`: Push real-time transitions (`Idle` -> `Thinking` -> `GateEvaluating` -> `PendingApproval` -> `Executing`).
  - `pending_approval`: Push approval requirement details with Action Digest and Parameter Diff.
  - `audit_event`: Push new audit entries for real-time audit log streaming.

## Usage Workflow

1. Start mock server:
   ```bash
   cd tools/mock_server && npm start
   ```
2. Trigger test scenario (e.g. "Inspection parameter change -> Approval Ask" or "Unauthorized tool call -> Immediate Deny").
3. Connect frontend `tools/web_dashboard` to `http://localhost:8080` (or `ws://localhost:8080/ws`).
