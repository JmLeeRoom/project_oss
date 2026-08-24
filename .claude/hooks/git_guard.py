#!/usr/bin/env python3
"""Cogito++ — git 브랜치 가드 (PreToolUse / Bash).

3-AI 가 한 저장소를 공유하므로 main 직접 커밋은 사고다.
체크리스트 협업 규약: 브랜치는 claude/*, codex/*, ag/* 로 분리하고 main 직접 커밋 금지.

차단 대상
  - main / master 에서의 git commit
  - git push --force / -f (모든 브랜치)
  - 다른 AI 소유 브랜치로의 push
"""
import json
import re
import subprocess
import sys

for _s in ("stdin", "stdout", "stderr"):
    try:
        getattr(sys, _s).reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

MY_PREFIX = "claude/"
PROTECTED = {"main", "master"}


def deny(reason: str) -> None:
    json.dump({
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        }
    }, sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")
    sys.exit(0)


def current_branch() -> str:
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            capture_output=True, text=True, timeout=5, encoding="utf-8",
        )
        return (out.stdout or "").strip()
    except Exception:
        return ""


def main() -> None:
    try:
        payload = json.load(sys.stdin)
    except Exception:
        sys.exit(0)

    if payload.get("tool_name") != "Bash":
        sys.exit(0)
    cmd = (payload.get("tool_input") or {}).get("command", "")
    if "git" not in cmd:
        sys.exit(0)

    if re.search(r"\bgit\s+push\b.*(--force\b|--force-with-lease\b|\s-f\b)", cmd):
        deny(
            "[git 가드] force push 는 차단됩니다. 3-AI 가 같은 원격을 공유하므로 "
            "다른 에이전트의 커밋이 소리 없이 사라집니다.\n"
            "→ 정말 필요하면 사람이 직접 실행하십시오."
        )

    if re.search(r"\bgit\s+commit\b", cmd):
        br = current_branch()
        if br in PROTECTED:
            deny(
                f"[git 가드] '{br}' 에 직접 커밋할 수 없습니다.\n"
                f"→ 먼저 브랜치를 만드십시오:  git switch -c {MY_PREFIX}<작업명>\n"
                f"→ 협업 규약: claude/* · codex/* · ag/* 로 분리, main 은 PR 로만 병합합니다."
            )
        if br and not br.startswith(MY_PREFIX):
            deny(
                f"[git 가드] 현재 브랜치 '{br}' 는 Claude 소유가 아닙니다.\n"
                f"→ Claude 는 '{MY_PREFIX}*' 브랜치에서만 커밋합니다.\n"
                f"→ 다른 에이전트의 브랜치를 이어받아야 한다면 사람 승인 아래 진행하십시오."
            )

    sys.exit(0)


if __name__ == "__main__":
    main()
