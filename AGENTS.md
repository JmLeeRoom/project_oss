# Cogito++ Codex Operating Rules

## Mission

Codex owns implementation and testing. Gemini owns prompts, architecture, specifications, configuration schemas, and public-header contracts.

The root Codex agent is the only writer in a Codex session. Subagents are read-only reviewers. When Gemini and Codex write concurrently, use separate branches and worktrees.

## Authority

Apply sources in this order:

1. The user's explicit instruction.
2. Approved human decisions, ADRs, and G0 resolutions.
3. The current Gemini-authored task prompt and its cited contract.
4. `docs/proposal/Cogito++_구현명세서.md` and `include/cogito/**`.
5. Requirements, checklist, and non-conflicting architecture background.

A task prompt defines assignment scope but cannot silently override an approved safety contract. If sources conflict, stop only the affected work, cite the clauses, and ask Gemini or the user directly. Do not create handoff documents.

## Two-Agent ownership

Gemini writes:

- `include/**`
- `docs/**`
- `config/**`
- `.agents/**`, except `.agents/skills/cogito-stage-owner/**`
- `GEMINI.md`
- `README.md`, licenses, security policy, and contribution documents

Codex writes:

- `src/**`
- `tests/**`, including `tests/web/**`
- `tools/**`, including dashboard, mock server, CLI, and native web host
- `cmake/**`, `bindings/**`, and `scripts/**`
- `CMakeLists.txt`, `CMakePresets.json`, and `vcpkg*.json`
- `AGENTS.md`, `.codex/**`, repository automation and CI
- `.agents/skills/cogito-stage-owner/**`

`scripts/ownership-policy.json` is the machine-readable source for this boundary.

## Working loop

For an implementation request:

1. Read the current Gemini prompt, the cited specification clauses, and affected headers.
2. Inspect the working tree and preserve unrelated user changes.
3. Choose one cohesive implementation scope. A formal S-stage ticket is optional unless the user or current prompt requires it.
4. Implement production behavior and relevant tests together.
5. Run the narrowest useful tests while iterating, then the affected regression suite.
6. Run `git diff --check` and inspect every changed and untracked path.
7. Report files, exact commands, exit codes, test counts, skipped checks, and remaining risks.

Use `$cogito-stage-owner` for substantive Cogito++ implementation work. Its evidence depth should be proportional to risk; routine changes do not require a separate stage-state ceremony.

## Safety and quality

- Never invent API signatures, port names, versions, licenses, timeouts, equipment values, or security defaults.
- Preserve stable errors, reason codes, deterministic bytes, lifetime rules, thread ownership, and audit ordering.
- For rejected safety paths, test state unchanged and write-call count zero when applicable.
- Use ASan, UBSan, TSan, leak checks, fuzzing, or browser security evidence where the affected risk requires them.
- Do not weaken assertions, skip failing tests, or change golden bytes merely to obtain a pass.
- A missing or Proposed contract remains unresolved after the collaboration control plane is simplified.

## Collaboration

Gemini delivers prompts and contract changes directly through `docs/**`, `include/**`, or the user conversation. Codex returns implementation results and concrete contract questions directly; no `docs/handoff/**` layer is used.

Read-only subagents may explore, audit, or independently verify risky work. Independent verification is recommended for safety-critical, security-sensitive, ABI, deterministic-serialization, and release changes, but is not mandatory for every routine edit.

## Git and publication

Do not commit, push, tag, merge, sign, rewrite history, or open a pull request unless the user explicitly asks. Never use destructive Git commands against unrelated work. Normal Git inspection is always allowed.

## MCP policy

Use local files and local build tools as implementation truth. Use connected GitHub tooling only for repository, issue, PR, review, and CI metadata. Do not access an audit database through MCP.
