---
name: cogito-stage-owner
description: Implement and verify Cogito++ production changes from Gemini-authored prompts, specifications, and public-header contracts. Use for C++, CMake, CLI, ABI, providers, adapters, dashboard, mock-server, browser, or test work. Do not use for prompt, specification, ADR, schema, or public-header authoring.
---

# Cogito++ Implementation Owner

Own one cohesive implementation change with its tests. A formal S0-S11 ticket is useful when the current prompt requires one, but is not mandatory for ordinary work.

## Prepare

1. Read `AGENTS.md`, the current Gemini-authored prompt, and only the cited contracts in [source-map.md](references/source-map.md).
2. Inspect the working tree and preserve unrelated changes.
3. Confirm that required API, safety, and error behavior is defined. If it is not, return a direct `NEEDS_GEMINI` question; do not create a handoff file or invent a contract.
4. Keep writes inside Codex-owned paths from `scripts/ownership-policy.json`.

## Implement and test

Implement production behavior and relevant tests in the same root-writer stream. Codex owns all implementation and tests, including web dashboard, mock server, native host, and browser suites.

Cover the normal path, boundaries, specified failures, and applicable state-unchanged/write-zero guarantees. Keep deterministic serialization and digest projection, the audit hash chain, approval state, and FSM transitions cohesive.

Use first-party sources for version-sensitive package, compiler, API, and license facts. Run the smallest useful check during iteration and the affected regression suite before delivery. Add sanitizer, fuzz, concurrency, leak, browser, CSP, or offline evidence when the change's risk requires it.

## Verify and deliver

Always run `git diff --check` and inspect changed and untracked paths. Report exact commands, exit codes, test counts, unsupported checks, and remaining risks.

Use an independent read-only reviewer for safety-critical, security-sensitive, ABI, deterministic-byte, or release work. Routine changes may use direct local verification.

End with one state:

- `READY_FOR_REVIEW`
- `NEEDS_GEMINI`
- `BLOCKED_BY_USER_DECISION`
- `FAILED_VERIFICATION`

Do not commit, push, tag, merge, sign, or publish unless the user explicitly asks.
