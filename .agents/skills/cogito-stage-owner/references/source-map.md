# Cogito++ source map

Use only the sources needed for the current implementation.

## Authority order

1. User instruction.
2. Approved human decisions, ADRs, and G0 resolutions.
3. Current Gemini-authored task prompt.
4. `docs/proposal/Cogito++_구현명세서.md` and `include/cogito/**`.
5. Requirements, checklist, and non-conflicting architecture background.

A prompt sets scope but does not silently override an approved safety contract.

## Routes

- Contracts and prompts: `docs/**`, `include/**`, `config/**` — Gemini.
- C++ implementation and all tests: `src/**`, `tests/**`, `cmake/**`, `bindings/**` — Codex.
- CLI, Web Host, dashboard, mock server, and supporting tools: `tools/**` — Codex.
- Build and automation: root CMake/vcpkg files and `scripts/**` — Codex.

If a required contract is missing or contradictory, stop only the affected work and ask Gemini or the user directly. Do not write a handoff document.
