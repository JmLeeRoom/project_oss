---
name: cogito-stage-owner
description: Implement and verify exactly one approved Cogito++ S0-S11 ticket with production code, tests, safety evidence, and an independent acceptance verdict. Use for Codex-owned C++, CMake, CLI, ABI implementation, native Web Host, or provider and adapter integration after contracts and prerequisite gates are approved. Do not use for G0 decisions, Claude-owned headers, docs, or schemas, Antigravity-owned dashboard or browser work, or general review-only questions.
---

# Cogito++ Stage Owner

Own exactly one approved implementation ticket from preparation through independently checked acceptance. Evaluate a stage Exit Gate only when the selected work is explicitly a stage-close task or the last required ticket. The root agent is the only writer. Subagents inspect and score; they do not edit.

## 1. Load the exact scope

Require one exact ticket identifier in `S0-01` through `S11-xx` form. A bare stage such as `S1` is not a ticket. Do not bundle tickets merely because they are adjacent.

Read, in this order:

1. `AGENTS.md` and `.codex/stage-state.json`.
2. The authority and routing rules in [source-map.md](references/source-map.md).
3. The checklist's G0 status, prerequisite gates, ticket definition, Exit Gate, verification commands, evidence requirements, and prohibited shortcuts.
4. Only the implementation-specification and verification-report sections named by that ticket.
5. Any approved ADR or human decision referenced by those sections.

Treat `TASK_PROMPT_*.md` files as coordination context, never as authority over an approved contract.

Stop before product writes when the ticket, prerequisite gate, G0 decision, contract, owner, or acceptance criterion is ambiguous.

## 2. Run the contract preflight

Delegate a read-only review to `contract_auditor` without a full-history fork (`fork_turns = "none"`, or the client equivalent). Give it the ticket identifier, exact files intended for change, relevant specification sections, prerequisites, and proposed tests in the message.

Act on its verdict:

- `PASS`: continue.
- `NEEDS_CLAUDE`: produce a compact handoff containing the conflicting clauses, affected API or invariant, and the decision required. Do not invent a header, schema, or ADR change.
- `BLOCKED`: stop and identify the unresolved human or external decision.

Record the accepted verdict in `.codex/stage-state.json` before changing product files. Use `phase = "preflight"`, the exact ticket, `baseline_head_oid` set to the current 40- or 64-hex HEAD, `g0_gate = "passed"`, `contract_review = "pass"`, a non-empty `contract_reference`, the proposed allowlist, and the complete planned verification object: matching ticket, exact commands, evidence directory, manifest path, verdict path, and a cleared verifier result. This preflight metadata is the only write allowed on `main` or `master`.

## 3. Lock the working boundary

After recording preflight state, create or switch to a branch named `codex/<ticket>-<slug>`. Never change product files on `main` or `master`. Set `phase = "implementation"` only after branch discovery confirms the active ticket branch.

Set the stage state to:

- the exact ticket identifier;
- `phase = "implementation"`;
- the approved G0 status;
- `contract_review = "pass"` and the approved contract or ADR reference;
- an exact, minimal `allowed_write_paths` list;
- `verification.ticket` set to the current active ticket, with prior verifier, HEAD, fingerprint, and stage-result fields cleared;
- publication and destructive-operation flags set to `false` unless the current user explicitly authorized one.

Do not add Claude-owned, Antigravity-owned, or shared-control files to the allowlist to bypass ownership.

`artifacts/verification/**`, `scripts/**`, `README.md`, and root task prompts are shared scope. Before writing an exact shared path, require explicit user scope approval and record that path plus the approval reference in `shared_write`; an agent-written boolean alone is not authority.

## 4. Implement code and tests as one unit

Translate the approved contract; do not create substitute behavior for missing requirements.

For every behavior changed, add or update tests in the same ticket. Cover, as applicable:

- the normal path;
- boundary values;
- each specified negative path and error code;
- zero-write or fail-closed guarantees;
- audit emission and ordering;
- concurrency, lifetime, and exception guarantees.

Keep these as single-writer cohesive units across successive tickets: CCJ serialization and digest projection, the audit hash chain, the approval state machine, and the FSM transition table. Preserve one root writer and consistent invariants, but modify only the files allowed for the current ticket; do not merge adjacent tickets to satisfy this rule.

## 5. Keep the feedback loop short

Run the narrowest relevant build and test after each coherent change. Use compiler diagnostics and actual test exit codes as the primary scoring signal.

Verify first-party facts before relying on version numbers, ports, API signatures, package names, or licenses. Raise a contract issue when the primary source and specification disagree.

For S4 through S6, run the required AddressSanitizer, UndefinedBehaviorSanitizer, and ThreadSanitizer variants when the platform supports them. If the platform cannot run a required sanitizer, record that as incomplete evidence rather than claiming success.

## 6. Assemble reproducible evidence

Create the evidence packet required by checklist section 18. Include at least:

- ticket and commit or working-tree identity;
- configuration and toolchain versions;
- exact commands as executed;
- exit codes and unedited relevant output;
- test result files, logs, and sanitizer output where required;
- hashes for artifacts the checklist requires to be byte-identical;
- explicit skipped, unsupported, or inconclusive checks;
- the mapping from acceptance criteria to evidence files.

Use repository-relative paths throughout. Write a non-empty `manifest.json` that binds the ticket, HEAD, workspace fingerprint, toolchain, every command and exit code, raw output file, acceptance mapping, and redaction result. Add an exact `evidence_files` object whose keys are every referenced command-output and acceptance-evidence path and whose values are their lowercase `sha256:<64-hex>` digests. Preserve a separate non-empty `verdict.json` from the independent verifier; it must carry the same ordered acceptance IDs and `manifest_sha256` for the exact manifest bytes it judged. The Stop hook rejects an empty directory, a stale ticket or fingerprint, changed approved shared content, expanded shared scope or verification plan, a changed evidence-file digest, a mismatched manifest digest or acceptance map, missing raw output, any nonzero required command, or any acceptance clause not marked `PASS`.

Do not report “verified” from memory or from a successful-looking excerpt.

## 7. Run independent acceptance and any applicable Exit Gate

Set `phase = "verification"`, then delegate the completed ticket and evidence packet to `exit_gate_verifier` without a full-history fork (`fork_turns = "none"`, or the client equivalent). Include the exact scope, commands, and evidence location in the message. The verifier stays read-only: it independently inspects raw results and reruns only demonstrably non-mutating checks. It returns exactly one ticket acceptance verdict:

- `PASS`;
- `FAIL`;
- `INCOMPLETE_EVIDENCE`.

The verifier must not repair the implementation it is judging. Fix failures in the root agent, regenerate evidence, and request a new verdict.

Ticket acceptance is not a stage Exit Gate. For example, accepting S1-05 does not close S1 while S1-06 or S1-07 remains. Ask the verifier for a separate `STAGE_EXIT_GATE` verdict only when every required ticket in that stage is complete or the user explicitly selected a stage-close verification task. Otherwise record `STAGE_EXIT_GATE: NOT_EVALUATED`.

After `PASS`, preserve the verifier's `verdict.json`, record the verifier ticket, exact commands, evidence directory, manifest and verdict paths, current HEAD, and workspace fingerprint in `.codex/stage-state.json`, then set `phase = "complete"` as described in `.codex/README.md`.

## 8. Hand off honestly

End with exactly one delivery state:

- `READY_FOR_HUMAN_REVIEW` when independent ticket acceptance passed; state separately whether the stage Exit Gate passed or remains unevaluated;
- `NEEDS_CLAUDE` for contract, header, schema, ADR, or traceability work;
- `NEEDS_ANTIGRAVITY` for dashboard, browser, CSP, or visual proof;
- `BLOCKED_BY_HUMAN_DECISION` for authority, legal, equipment, identity-provider, CI-signing, or release-scope choices;
- `FAILED_GATE` when evidence proves the implementation is not ready.

Report changed files, commands and exit codes, evidence location, verifier verdict, remaining risks, and the requested next owner. Do not commit, push, tag, merge, sign, or publish unless the user explicitly asks.
