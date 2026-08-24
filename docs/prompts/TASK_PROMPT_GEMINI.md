# [Gemini Task] Cogito++ contract and implementation prompt preparation

> Role: Gemini owns prompts, specifications, ADR/G0 maintenance, configuration schemas, and public-header contracts.
> Executor boundary: Codex owns all implementation and tests, including web dashboard, mock server, native host, and browser suites.

## First task

1. Review the preserved technical assets in `docs/proposal/**`, `docs/g0/**`, `docs/adr/**`, and `include/cogito/**`.
2. Preserve historical authorship, but replace future legacy-agent actions with the Gemini-Codex 2-Agent workflow.
3. Resolve or explicitly retain every Proposed decision. Organizational simplification does not constitute technical approval.
4. Remove stale references to deleted `.claude/**`, `CLAUDE.md`, `docs/handoff/**`, and audit-log files from maintained contracts.
5. Fill `docs/prompts/TASK_PROMPT_CODEX.md` with one cohesive implementation assignment, exact contract references, Codex-owned paths, and risk-proportional tests.

Do not implement product code or modify tests. Ask the user directly for human-only safety, equipment, identity, supply-chain, legal, or release decisions.
