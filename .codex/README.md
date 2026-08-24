# Cogito++ Codex control plane

This directory supports the Gemini-Codex 2-Agent model.

- Gemini owns prompts, specifications, schemas, and public-header contracts.
- Codex owns all implementation, build logic, automation, and tests, including web.
- The root Codex agent writes; subagents review read-only.

## Active hooks

`.codex/hooks.json` enables only:

- `SessionStart`: inject the two-agent role summary.
- `SubagentStart`: reinforce the read-only reviewer boundary.
- `PreToolUse`: enforce ownership, path normalization, and destructive-Git safety.

There is no Stop/Exit-Gate blocker, stage-state file, command-plan allowlist, manifest fingerprint, or ticket-state requirement.

## Validation

Run from the repository root:

```powershell
py -3 -B .codex\hooks\cogito_hooks.py --validate-policy
py -3 -B -m unittest discover -s .codex\hooks\tests -p "test_*.py" -v
git diff --check
```

Inspect one path assignment with:

```powershell
py -3 -B .codex\hooks\cogito_hooks.py --owner tests/web/example.test.js
```

## Git safety

Repository edits use exact-path write tools so ownership can be checked; indirect scripts and filesystem mutation through shell are blocked. Build and test runners remain available. Subagent shell access is limited to an explicit read-only command set.

Normal read-only Git commands and canonical user-authorized feature-branch commit/push forms are not blanket-denied. Merge and other worktree-changing Git forms require a separately reviewed workflow. Destructive history/ref operations, helper injection, force/bulk/delete pushes, protected-branch commits or pushes, and commits containing Gemini-owned paths remain blocked.

`.codex/rules/safety.rules` still prompts for external publication. The hook is a guardrail, not an authorization source; Codex does not publish unless the user explicitly asks.

## Review agents

The optional contract reviewer and implementation verifier are read-only. Use them for ambiguous or high-risk work, not as a mandatory ceremony for every edit.
