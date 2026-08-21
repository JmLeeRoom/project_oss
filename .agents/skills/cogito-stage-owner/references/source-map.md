# Cogito++ source map

Use this map to limit reading and to resolve routing. It does not replace the authoritative documents.

## Authority order

Apply sources in this order. Stop on unresolved conflict at the highest applicable level.

1. The user's explicit instruction and approved human decisions or ADRs.
2. `Cogito++_구현명세서.md`.
3. `Cogito++_구현_요구사항.md` together with `Cogito++_구현_요구사항_검증보고서.md`.
4. `Cogito++_개발_작업체크리스트.md` for sequence, gates, commands, and evidence.
5. `Cogito++_OSS_기술스택_아키텍처.md` for explanatory architecture context.
6. `TASK_PROMPT_*.md` for coordination context only.

## Mandatory checklist slices

For one ticket, read only the portions necessary to establish:

- G0 disposition and any human-only decision;
- prerequisites and prior-stage Exit Gates;
- the ticket's inputs, outputs, acceptance criteria, and prohibited shortcuts;
- its exact verification commands and evidence requirements;
- checklist section 18 evidence-packet rules.

Read the corresponding implementation-specification clauses referenced by the ticket. Do not rely on remembered clauses from an earlier stage.

## Stage routing

- `S0`: repository, toolchain, CMake, package and consumer smoke infrastructure.
- `S1`: CCJ, canonical bytes, digest, identifiers, and golden vectors.
- `S2`: Action, tool schemas and contracts, registry, configuration, and their projections.
- `S3`: FSM and transition-table behavior.
- `S4`: identity, policy, budgets, approval, permit semantics, concurrency, and lifetime.
- `S5`: PermissionGate plus audit ordering, persistence, recovery, and hash chains.
- `S6`: conversation, inference, invoker, AgentLoop, commit protocol, and core integration.
- `S7`: C ABI v1.1, CLI, exports, installation consumers, and minimum C# P/Invoke.
- `S8`: HTTP/OpenAI-compatible and llama.cpp providers under approved supply-chain contracts.
- `S9`: OPC UA adapter and its fault-injection and HMI demonstrations.
- `S10`: split ownership. Codex handles the native Web Host, CMake, HTTP/SSE backend, and native tests in Codex-owned paths; Antigravity handles the React dashboard, mock server, browser/CSP/visual proof, and web tests in Antigravity-owned paths. Route contract and threat-model documents to Claude.
- `S11`: release, reproducibility, signing, legal, and operational closure; many items require human authority.

## Ownership routing

- Codex writes implementation and tests only in the paths granted by `AGENTS.md` and the active ticket allowlist.
- Claude owns public headers, contracts, schemas, ADRs, specifications, and traceability documents.
- Antigravity owns the dashboard, mock web server, browser tests, CSP proof, and visual evidence.
- The user owns decisions involving supply chain, real equipment values, identity authority, segregation-of-duties exceptions, CI and signing principals, legal approval, and release scope.

## Known conflicts that must not be silently resolved

- The checklist's G0 Exit Gate blocks S0 product work, while `TASK_PROMPT_CODEX.md` requests immediate S0 work.
- `TASK_PROMPT_CODEX.md` requests public-header skeletons even though those paths belong to Claude.
- `TASK_PROMPT_ANTIGRAVITY.md` mentions WebSocket flow while the authoritative contract may require SSE-only behavior.

Treat these as routing warnings. Re-evaluate them against the current approved documents before each affected ticket.
