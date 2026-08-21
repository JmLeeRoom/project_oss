#!/usr/bin/env python3
"""Cogito++ — Claude Code PreToolUse 가드.

두 가지를 막는다.
  1) 소유권 위반 — scripts/ownership-policy.json 에서 owner != claude 인 경로 쓰기
  2) 제어문자 혼입 — 텍스트 파일에 C0 제어문자(\\t \\n \\r 제외)를 써서 파일을
     바이너리로 만드는 사고. 실제로 Cogito++_구현명세서.md 에서 발생했다(G0 인접 사고).

입력  : Claude Code PreToolUse payload (stdin JSON)
        {"tool_name":"Write","tool_input":{"file_path":..,"content":..}}
출력  : 차단 시 permissionDecision=deny, 통과 시 아무것도 출력하지 않음(정상 권한 흐름 유지)

주의  : Antigravity 의 scripts/guard-scope.ps1 과 stdin 스키마가 다르다
        (CC: tool_name/tool_input.file_path, AG: toolCall.name/toolCall.args.TargetFile).
        정책(ownership-policy.json)만 공유하고 어댑터는 도구별로 따로 둔다.
"""
import json
import os
import sys

# Windows 기본 stdio 인코딩은 cp949 다. Claude Code 는 UTF-8 로 payload 를 준다.
# 이걸 맞추지 않으면 한글이 포함된 경로가 mojibake 가 되어 relpath 가 어긋나고
# 가드가 '조용히 통과'한다. 파이프 테스트에서 실제로 재현됐다.
for _s in ("stdin", "stdout", "stderr"):
    try:
        getattr(sys, _s).reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ME = "claude"
ALLOWED_OWNERS = {ME, "shared"}

# 제어문자 검사 대상 확장자. 바이너리 픽스처는 제외한다.
TEXT_EXT = {
    ".md", ".txt", ".json", ".yaml", ".yml", ".toml", ".cmake", ".py", ".ps1",
    ".sh", ".hpp", ".h", ".cpp", ".cc", ".c", ".ts", ".tsx", ".js", ".jsx",
    ".css", ".html", ".sql", ".xml", ".ini", ".cfg", ".gitignore", ".gitattributes",
}
BAD_CTRL = {c for c in range(0x00, 0x20)} - {0x09, 0x0A, 0x0D}


def repo_root() -> str:
    # .claude/hooks/cc_guard.py -> 저장소 루트
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


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


def warn_and_pass(msg: str) -> None:
    json.dump({"systemMessage": msg}, sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")
    sys.exit(0)


def load_rules(root: str):
    path = os.path.join(root, "scripts", "ownership-policy.json")
    if not os.path.isfile(path):
        return None
    # PowerShell 이 쓴 파일에 BOM 이 붙을 수 있다 -> utf-8-sig
    with open(path, encoding="utf-8-sig") as f:
        policy = json.load(f)
    rules = [(r["prefix"].replace("\\", "/"), r["owner"]) for r in policy.get("rules", [])]
    rules.sort(key=lambda x: len(x[0]), reverse=True)  # 가장 긴 prefix 우선
    return rules


def owner_of(rel: str, rules) -> str:
    for prefix, owner in rules:
        if rel == prefix or rel.startswith(prefix):
            return owner
    return "shared"


def main() -> None:
    try:
        payload = json.load(sys.stdin)
    except Exception:
        sys.exit(0)  # 파싱 불가 -> 가드가 작업을 막지 않는다

    tool = payload.get("tool_name", "")
    if tool not in ("Write", "Edit", "NotebookEdit"):
        sys.exit(0)

    ti = payload.get("tool_input") or {}
    target = ti.get("file_path") or ti.get("notebook_path") or ""
    if not target:
        sys.exit(0)

    root = repo_root()
    abs_target = os.path.abspath(target)

    # ── 1. 소유권 ────────────────────────────────────────────────
    try:
        rel = os.path.relpath(abs_target, root).replace("\\", "/")
    except ValueError:  # 다른 드라이브
        rel = None

    inside_repo = rel is not None and not rel.startswith("..")
    if inside_repo:
        rules = load_rules(root)
        if rules is None:
            warn_and_pass(
                "[cc_guard] scripts/ownership-policy.json 을 찾을 수 없어 소유권 검사를 건너뜁니다. "
                "3-AI 경계가 강제되지 않는 상태입니다."
            )
        owner = owner_of(rel, rules)
        if owner not in ALLOWED_OWNERS:
            deny(
                f"[역할 경계] '{rel}' 의 소유자는 '{owner}' 입니다. Claude 는 include/**, docs/**, "
                f"config/**, .claude/**, Cogito++_*.md 만 씁니다.\n"
                f"→ 파일을 고치지 말고 체크리스트 §1-2 형식의 이슈로 {owner} 에게 인계하십시오.\n"
                f"→ 경계 자체를 바꿔야 한다면 scripts/ownership-policy.json 을 먼저 사람 승인 아래 수정합니다."
            )

    # ── 2. 제어문자 ──────────────────────────────────────────────
    ext = os.path.splitext(abs_target)[1].lower()
    if ext in TEXT_EXT:
        body = ti.get("content") or ti.get("new_string") or ti.get("new_source") or ""
        if isinstance(body, str):
            hits = sorted({ord(ch) for ch in body if ord(ch) in BAD_CTRL})
            if hits:
                shown = ", ".join(f"U+{h:04X}" for h in hits[:8])
                deny(
                    f"[제어문자 차단] 텍스트 파일에 원시 제어문자가 들어 있습니다: {shown}\n"
                    f"→ 파일이 바이너리로 분류되어 grep·diff·리뷰가 전부 깨집니다.\n"
                    f"→ 제어문자를 예시로 '표기'하려는 의도라면 이스케이프 문자열(\\\\u0000)로 쓰십시오.\n"
                    f"→ 실제로 Cogito++_구현명세서.md §3-1-a 골든 벡터 표에서 이 사고가 났습니다."
                )

    sys.exit(0)


if __name__ == "__main__":
    main()
