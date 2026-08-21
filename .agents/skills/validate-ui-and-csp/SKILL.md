---
name: validate-ui-and-csp
description: >-
  Automated verification skill to test the Cogito++ Web Dashboard using headless browser automation.
  Asserts 0 CSP violations with strict style-src 'self', verifies 1024x768 viewport layout (no vertical scroll on Authority Area),
  checks 56px minimum touch target size, and validates 24px button separation.
---

# Validate UI and CSP Compliance

This skill defines the step-by-step workflow for Antigravity to visually and programmatically verify the Cogito++ Web Dashboard against safety and security invariants.

## Verification Checklist

1. **G0-33 CSP Compliance (Zero Violations)**:
   - Target CSP Header: `default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self' ws:; font-src 'self'; frame-ancestors 'none';`
   - Assert `console.error` and `SecurityPolicyViolationEvent` count == 0.
   - If React Flow / Recharts trigger inline-style violations, flag for replacement with pure SVG.

2. **§12-8 Authority Area Layout (1024x768 No-Scroll)**:
   - Set viewport to `1024 x 768`.
   - Assert that the Approval Decision Box (Tool Name, Parameter Diff, Risk Level, Action Digest, Approve/Reject buttons) is fully visible without scrolling (`scrollHeight <= clientHeight`).

3. **§12-8 Touch Target & Spacing**:
   - Measure Approve and Reject button bounding boxes: `width >= 56px` and `height >= 56px`.
   - Measure gap between Approve and Reject buttons: `gap >= 24px`.

## Execution Workflow

1. Start Mock Server (`tools/mock_server`) or Dev Server (`tools/web_dashboard`).
2. Run automated Playwright/Puppeteer script:
   ```bash
   npx playwright test tests/web/csp-and-layout.spec.ts
   ```
3. Capture full-page screenshot at `1024x768` and save to artifacts directory.
4. Report pass/fail status with exact measured bounding box metrics and CSP violation logs.
