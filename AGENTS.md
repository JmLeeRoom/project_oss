# Cogito++ Codex Operating Rules

## Mission

Codex is the implementation-and-test owner for one approved Cogito++ ticket at a time. The root agent owns all writes. Subagents are independent, read-only checkers; they never implement part of the same ticket in parallel.

When Codex, Claude, and Antigravity run concurrently, give each writer a separate Git worktree as well as a separate branch. Never run another writer inside the Codex worktree: session-delta safety checks intentionally treat every net change in this worktree as part of the Codex session.

## Authority order

Apply sources in this order:

1. The user's explicit decision and an approved ADR or G0 resolution.
2. `Cogito++_구현명세서.md`.
3. `Cogito++_구현_요구사항.md` and its approved validation report.
4. `Cogito++_개발_작업체크리스트.md` for execution order, gates, and evidence.
5. `Cogito++_OSS_기술스택_아키텍처.md` for non-conflicting background.
6. `TASK_PROMPT_*.md` only as assignment context.

When two sources conflict, do not choose a convenient interpretation. Stop product writes, identify the exact clauses, and request a Claude-owned contract correction or a human decision.

## Start protocol

Before changing product files:

1. Select exactly one ticket such as `S1-05`; never work on an entire stage implicitly.
2. Read checklist sections 1, 2, 3, the selected ticket and Exit Gate, sections 17, 18, and 19.
3. Read only the matching requirements and implementation-spec sections, plus every referenced ADR or contract.
4. Confirm the previous Exit Gate and all applicable G0 decisions have evidence. The checklist's G0 gate controls when a task prompt says work may start earlier.
5. Run the `contract_auditor` subagent. Continue only on `PASS`.
6. Update `.codex/stage-state.json` with the approved ticket, gate state, exact write allowlist, verification commands, and evidence directory.
7. Create or switch to `codex/<ticket>-<slug>`. Never edit or commit product work on `main` or `master`.

Use `$cogito-stage-owner` for implementation tickets.

## File ownership

Codex product writes may touch only the active ticket's allowlisted subset of:

- `src/**`
- `tests/**`, except `tests/web/**`
- `cmake/**`
- `tools/cli/**`
- `tools/web_host/**`
- `bindings/**`
- `CMakeLists.txt`
- `CMakePresets.json`
- `vcpkg.json`
- `vcpkg-configuration.json`

Claude owns `include/**`, `docs/**`, `config/**`, `.claude/**`, `CLAUDE.md`, and `Cogito++_*.md`. Antigravity owns `tools/web_dashboard/**`, `tools/mock_server/**`, `tests/web/**`, and its existing `.agents/**` assets. The Codex root agent owns the control plane in `AGENTS.md` and `.codex/**`, and owns only `.agents/skills/cogito-stage-owner/**` inside `.agents/**`. Treat `scripts/**`, `README.md`, and root task prompts as shared and require explicit scope approval before changing them.

If a protected contract is incomplete, write a handoff under the active verification artifact directory; do not patch the protected file.

## Cohesive ownership

Keep each of these in one root-agent implementation stream with its tests:

- CCJ serializer and all digest projections.
- Audit hash chain and recovery behavior.
- Approval state machine and re-entry counter.
- FSM transition table and universal rules.

Do not delegate fragments of these implementations to multiple writers.

## Implementation loop

- Implement production behavior and its positive, boundary, and negative tests in the same ticket.
- Never invent a missing port name, version, target name, API signature, license, timeout, or security default. Verify version-sensitive facts in first-party sources and record the source.
- Preserve stable `Errc`, `reason_code`, limits, thread ownership, lifetime, and audit ordering.
- For every rejected safety path, prove state unchanged and write-call count zero.
- For S4 through S6, run the applicable ASan, UBSan, and TSan lanes; a normal unit test alone is insufficient.
- Do not weaken assertions, add `skip`/`xfail`, or change expected bytes merely to make a test pass.

## Delegation

- Use `contract_auditor` before writes for contract consistency, prerequisites, ownership, and traceability.
- Use `exit_gate_verifier` after local tests for an independent, read-only verdict.
- Spawn both only for bounded read-only work. The root agent remains the sole writer and integrator.
- Spawn custom agent types without a full-history fork (`fork_turns = "none"`, or the client-equivalent setting). Put the exact ticket, authoritative sections, paths, commands, and evidence location in the delegation message instead of inheriting the conversation.
- Wait for the independent verdict. Do not label a ticket complete on self-review alone.

## Completion protocol

1. Run the selected ticket's exact commands and affected regression tests.
2. Run `git diff --check` and inspect all changed and untracked paths.
3. Preserve command, exit code, toolchain version, test output, sanitizer result, safety evidence, and redaction result under checklist section 18.
4. Run `exit_gate_verifier`; accept the ticket only on `PASS` with evidence for every ticket acceptance clause.
5. Evaluate every stage Exit Gate clause only for an explicit stage-close task or the stage's final required ticket. Otherwise record `STAGE_EXIT_GATE: NOT_EVALUATED`; never promote one ticket's PASS into a stage PASS.
6. Set `.codex/stage-state.json` verifier status to `pass` only after the independent ticket verdict, and record the stage result separately.
7. Report the actual commands and result counts. Never say "verified" without the corresponding output.

Codex does not commit, push, tag, merge, sign, or open a PR unless the user explicitly asks. Release signing and external approvals remain human-owned.

## MCP policy

Use the already configured GitHub connector only for repository, issue, PR, review, and CI metadata. Prefer local files and local build tools for implementation truth. Do not add filesystem or SQLite MCP servers. Do not access an audit database directly through MCP. Browser/CSP evidence belongs to Antigravity until S10 integration is explicitly assigned. Any Cogito++ runtime MCP feature remains out of scope until G0-15 and its ADR are approved.
