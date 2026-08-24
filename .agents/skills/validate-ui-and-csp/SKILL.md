---
name: validate-ui-and-csp
description: >-
  Automated verification skill to test the Cogito++ Web Dashboard using headless browser automation.
  Asserts 0 CSP violations with strict default-src 'none' / style-src 'self', verifies 1024x768 viewport layout (no vertical scroll on Authority Area),
  checks 56px minimum touch target size, 24px button separation, 2-step approval confirmation, and asserts write 0 on unauthorized paths.
---

# Validate UI and CSP Compliance

This skill defines the step-by-step workflow for Codex to visually and programmatically verify the Cogito++ Web Dashboard against Gemini-owned safety and security acceptance criteria.

## Verification Checklist

1. **G0-33 CSP Compliance (Zero Violations)**:
   - Target CSP Header (§12-6): `default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; font-src 'self'; connect-src 'self'; worker-src 'self'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'; upgrade-insecure-requests`
   - Assert `console.error` and `SecurityPolicyViolationEvent` count == 0.
   - Assert response security headers: HSTS, nosniff, no-referrer, Cache-Control: no-store on `/api/*`.

2. **W6 Single Command Path**:
   - Assert inbound WebSocket upgrade requests are rejected with 400 Bad Request.
   - Assert SSE stream (`GET /api/events`) is the single event transport.

3. **§12-8 Authority Area Layout (1024x768 No-Scroll)**:
   - Set viewport to `1024 x 768`.
   - Assert that the Approval Authority Card (Tool Name, Parameter Table, Risk Badge with text, Action Digest, Expiry countdown, Approve/Reject buttons) is fully visible without scrolling (`scrollHeight <= clientHeight` on `#approval-authority-card`).

4. **§12-8 Touch Target & Spacing & 2-Step Confirmation**:
   - Measure Approve and Reject button bounding boxes: `width >= 56px` and `height >= 56px`.
   - Measure gap between Approve and Reject buttons: `gap >= 24px`.
   - Assert 2-step confirmation on Approve button (first click arms confirmation state, second click dispatches approval).

5. **Invariant 8 / §11 S10 Safety Assertion**:
   - Assert that on unapproved, forged digest/nonce, expired, and rejected paths, the write call count remains **0**.
   - Assert that write execution occurs only after valid 3-way verified approval.

## Execution Workflow

1. Start Mock Server (`tools/mock_server`):
   ```bash
   node tools/mock_server/src/server.js
   ```
2. Run automated CDP verification test:
   ```bash
   node tests/web/csp-and-hmi.test.js
   ```
3. Capture full-page screenshot at `1024x768` (`tests/web/screenshot_1024x768.png`).
4. Report pass/fail status with exact measured bounding box metrics, CSP violation logs, and write call count.

