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

────────────────────────────────────────────────────────────────────────────
개정 2026-08-24 — docs/STATUS-AUDIT-2026-08-24.md §2-④ · §2-⑤ 의 실측 결함 3건 수정.

⚠ 초판의 "새로 허용되는 경로는 없다" 는 **거짓이었다.** [3] 이 파일 rule 을 startswith 에서
  정확일치로 좁히면서, 이전에 codex 소유로 매칭되어 deny 되던 형제 경로가 어떤 rule 에도
  걸리지 않아 fallback 인 shared 로 떨어졌다. 실측: `AGENTS.md.bak` · `CMakeLists.txt.bak` ·
  `vcpkg.json.tmp` 가 전부 ALLOW 로 뒤집혔다. [5] 의 2단 매칭으로 되돌렸다.
  자기 개정이 안전 방향이라는 주장도 실측으로 확인해야 한다는 사례로 남긴다.

  [1] fail-open -> fail-closed
      기존: 정책 파일이 없으면 warn_and_pass() 로 '통과'시켰다.
            JSON 이 깨지면 json.load 예외가 잡히지 않아 프로세스가 exit 1 로 죽었고,
            그것도 PreToolUse deny 가 아니므로 결과는 역시 통과였다.
      실측: 정책 파일 하나만 지우면 Claude 의 소유권 경계가 통째로 사라졌다.
            나머지 두 가드(guard-scope.ps1, cogito_hooks.py)는 fail-closed 였다.
      수정: 정책을 신뢰할 수 없으면 무조건 deny.

  [2] case_sensitive: false 미구현
      기존: rel.startswith(prefix) 를 대소문자 그대로 비교했다.
      실측: 'SRC/foo.cpp' 와 'Include/cogito/fsm.hpp' 가 둘 다 allow 됐다.
            NTFS 는 대소문자를 구분하지 않으므로 'SRC/foo.cpp' 는 'src/foo.cpp' 와 같은 파일이다.
            즉 대문자 경로로 Codex 의 src/·tests/ 를 덮어쓸 수 있었다.
      수정: guard_contract.case_sensitive 를 읽어 casefold 비교.

  [3] 디렉터리 prefix 와 파일 prefix 를 구분하지 않음
      기존: 'CLAUDE.md.bak' 이 'CLAUDE.md' 규칙에 매칭됐다.
      수정: guard_contract 의 directory_prefix_suffix / special_root_prefixes 규약대로
            '/' 로 끝나면 디렉터리 prefix, special_root_prefixes 면 startswith,
            그 외는 정확 일치.

  [4] 정책 파일 자기 수정 차단 (§2-⑤)
      scripts/ 는 'shared' 라 Claude 가 자기 경계를 정의하는 파일을 무승인으로 고칠 수 있었다.
      가드의 deny 메시지가 '정책 파일을 사람 승인 아래 먼저 고치라'고 안내하는데
      그 수정 자체가 무승인으로 가능해 우회 절차를 알려주는 꼴이었다.
      → cc_guard 는 이제 정책 파일 쓰기를 항상 deny 한다. 이것은 자기 제한이다.
      ※ 같은 결함이 guard-scope.ps1(shared) 에도 남아 있고, 정책 파일에 human-write-only
        rule 을 넣는 근본 수정은 사람 판정 대상이다(STATUS-AUDIT §3-A-①·⑤ 계열).

  [5] 2단 매칭 — [3] 이 만든 구멍을 막는다
      1단(정밀): '/' 로 끝나면 디렉터리, special_root_prefixes 면 startswith, 그 외 정확일치.
      2단(안전망): 1단에서 못 찾으면 **평범한 longest startswith** 로 한 번 더 본다.
                   'AGENTS.md.bak' 은 여기서 'AGENTS.md' rule 에 걸려 codex 로 귀속된다.
      둘 다 실패해야 fallback_owner(shared) 다.
      결과: 'CLAUDE.md.bak' 은 claude(자기 파일이므로 허용), 'AGENTS.md.bak' 은 codex(차단).
      기존 동작보다 넓게 허용되는 경로가 없다 — 아래 미결 항목을 제외하면.

  [미결] 남아 있는 우회로 — 사람 판정 필요
      (a) Bash 툴 경유 쓰기(리다이렉션·heredoc·cp·mv·python -c)는 이 가드를 통째로 우회한다.
          이 훅은 Write/Edit/NotebookEdit 세 툴만 본다. 차단은 오탐 비용이 크므로
          '차단' 이 아니라 '결정 로그' 로 감사 경로를 두는 안을 검토 대상으로 남긴다.
      (b) ALLOWED_OWNERS 가 'shared' 를 무조건 포함한다. 정책은 shared 를
          "정확한 경로에 대한 사용자 승인 후에만 쓰기 가능" 으로 정의하는데 이 가드는
          승인 여부를 알 수 없어 통과시킨다. 승인 표현 방법이 정해지면 좁혀야 한다.
────────────────────────────────────────────────────────────────────────────
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

# 소유권 단일 소스. 이 파일 자체는 Claude 도 쓰지 않는다(위 [4]).
POLICY_REL = "scripts/ownership-policy.json"

# guard_contract 기본값 — 정책 파일에 키가 없을 때만 쓴다.
# 정책이 다른 값을 명시하면 그쪽이 이긴다. 알 수 없는 값이면 fail-closed.
DEFAULT_CONTRACT = {
    "algorithm": "longest_prefix",
    "case_sensitive": False,
    "separator": "/",
    "directory_prefix_suffix": "/",
    "special_root_prefixes": [],
    "fallback_owner": "shared",
}

# 제어문자 검사 대상 확장자. 바이너리 픽스처는 제외한다.
TEXT_EXT = {
    ".md", ".txt", ".json", ".yaml", ".yml", ".toml", ".cmake", ".py", ".ps1",
    ".sh", ".hpp", ".h", ".cpp", ".cc", ".c", ".ts", ".tsx", ".js", ".jsx",
    ".css", ".html", ".sql", ".xml", ".ini", ".cfg",
}
# 확장자가 없는 텍스트 파일. splitext('.gitignore') 는 ('.gitignore','') 를 주므로
# 확장자 집합으로는 절대 잡히지 않는다 — basename 으로 따로 본다.
TEXT_BASENAME = {
    ".gitignore", ".gitattributes", ".editorconfig", ".clang-format", ".clang-tidy",
    "CMakeLists.txt", "Dockerfile", "Makefile", "LICENSE", "NOTICE",
}
BAD_CTRL = {c for c in range(0x00, 0x20)} - {0x09, 0x0A, 0x0D}


def repo_root() -> str:
    # .claude/hooks/cc_guard.py -> 저장소 루트
    here = os.path.realpath(os.path.abspath(__file__))
    return os.path.realpath(os.path.join(os.path.dirname(here), "..", ".."))


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


def deny_policy_unusable(detail: str) -> None:
    """정책을 신뢰할 수 없으면 통과시키지 않는다 — fail-closed.

    통과시키면 3-AI 경계가 사라진 채로 쓰기가 진행된다. 조용히 계속하는 것이 가장 나쁘다.
    """
    deny(
        f"[가드 fail-closed] 소유권 정책을 읽을 수 없어 모든 쓰기를 차단합니다.\n"
        f"  사유: {detail}\n"
        f"  파일: {POLICY_REL}\n"
        f"→ 정책 파일을 복구하십시오. 복구 전에는 어떤 파일도 쓰지 않습니다.\n"
        f"→ 가드를 끄는 것으로 우회하지 마십시오. 경계 없이 쓰면 다른 AI 의 산출물을 덮어씁니다."
    )


def load_policy(root: str):
    """(rules, contract) 를 돌려준다. 실패하면 돌아오지 않는다(deny 후 종료).

    rules 는 (prefix, owner, kind) 목록이며 가장 긴 prefix 가 앞에 온다.
    kind 는 'dir' | 'root_prefix' | 'exact'.
    """
    path = os.path.join(root, "scripts", "ownership-policy.json")
    if not os.path.isfile(path):
        deny_policy_unusable("파일이 없습니다")

    try:
        # PowerShell 이 쓴 파일에 BOM 이 붙을 수 있다 -> utf-8-sig
        with open(path, encoding="utf-8-sig") as f:
            policy = json.load(f)
    except Exception as e:  # JSON 파손·인코딩 오류·권한 오류 전부 fail-closed
        deny_policy_unusable(f"{type(e).__name__}: {e}")

    if not isinstance(policy, dict):
        deny_policy_unusable("최상위가 객체가 아닙니다")

    # ── guard_contract ────────────────────────────────────────────
    # 정책이 명시한 값을 쓰되, 이 가드가 구현하지 않은 값이면 fail-closed 한다.
    # 정책 전체를 '완전 일치'로 요구하지는 않는다 — 다른 AI 가 무관한 필드를 하나
    # 추가하는 것만으로 이 가드가 잠기면 교차 DoS 가 된다(STATUS-AUDIT §2-⑤).
    raw = policy.get("guard_contract") or {}
    if not isinstance(raw, dict):
        deny_policy_unusable("guard_contract 가 객체가 아닙니다")

    contract = dict(DEFAULT_CONTRACT)
    contract.update({k: raw[k] for k in DEFAULT_CONTRACT if k in raw})

    if contract["algorithm"] != "longest_prefix":
        deny_policy_unusable(
            f"이 가드는 algorithm='longest_prefix' 만 구현합니다 "
            f"(정책 값: {contract['algorithm']!r})"
        )
    if contract["separator"] != "/":
        deny_policy_unusable(
            f"이 가드는 separator='/' 만 구현합니다 (정책 값: {contract['separator']!r})"
        )
    if not isinstance(contract["case_sensitive"], bool):
        deny_policy_unusable("guard_contract.case_sensitive 가 boolean 이 아닙니다")

    dir_suffix = contract["directory_prefix_suffix"]
    if not isinstance(dir_suffix, str) or not dir_suffix:
        deny_policy_unusable("guard_contract.directory_prefix_suffix 가 비어 있습니다")

    specials = contract["special_root_prefixes"]
    if not isinstance(specials, list) or any(not isinstance(s, str) for s in specials):
        deny_policy_unusable("guard_contract.special_root_prefixes 가 문자열 배열이 아닙니다")

    # ── rules ─────────────────────────────────────────────────────
    raw_rules = policy.get("rules")
    if not isinstance(raw_rules, list) or not raw_rules:
        deny_policy_unusable("rules 가 비어 있거나 배열이 아닙니다")

    rules = []
    for r in raw_rules:
        if not isinstance(r, dict) or "prefix" not in r or "owner" not in r:
            deny_policy_unusable(f"rules 항목에 prefix/owner 가 없습니다: {r!r}")
        prefix = str(r["prefix"]).replace("\\", "/")
        owner = str(r["owner"])
        if prefix.endswith(dir_suffix):
            kind = "dir"
        elif prefix in specials:
            kind = "root_prefix"
        else:
            kind = "exact"
        rules.append((prefix, owner, kind))

    # 가장 긴 prefix 우선
    rules.sort(key=lambda x: len(x[0]), reverse=True)
    return rules, contract


def owner_of(rel: str, rules, contract) -> str:
    """2단 매칭. 가장 긴 prefix 우선. 둘 다 실패해야 fallback_owner.

    1단(정밀) — guard_contract 규약대로:
        'docs/'      디렉터리 prefix   -> startswith
        'Cogito++_'  special_root      -> startswith
        'CLAUDE.md'  파일              -> 정확 일치
    2단(안전망) — 1단에서 못 찾으면 평범한 longest startswith 로 한 번 더 본다.

    2단이 없으면 'AGENTS.md.bak' 이 어떤 rule 에도 안 걸려 shared 로 떨어지고,
    shared 는 ALLOWED_OWNERS 에 있으므로 Claude 가 Codex 의 파일 사본을 쓸 수 있게 된다.
    실제로 초판 개정에서 그 구멍이 났다(파일 머리 [5] 참조).
    """
    fold = (lambda s: s) if contract["case_sensitive"] else (lambda s: s.casefold())
    target = fold(rel)

    # ── 1단: 정밀 매칭 ───────────────────────────────────────────
    for prefix, owner, kind in rules:
        p = fold(prefix)
        if kind == "dir":
            # 'docs/' 는 'docs/a.md' 에 매칭. 'docs' 자체(디렉터리 노드)도 소유로 본다.
            if target.startswith(p) or target == p.rstrip("/"):
                return owner
        elif kind == "root_prefix":
            # 'Cogito++_' 는 'Cogito++_구현명세서.md' 에 매칭
            if target.startswith(p):
                return owner
        else:
            # 'CLAUDE.md' 는 'CLAUDE.md' 에만 매칭
            if target == p:
                return owner

    # ── 2단: 안전망 (형제 경로를 원 소유자에게 귀속) ─────────────
    # 'AGENTS.md.bak' -> 'AGENTS.md' rule -> codex.  'CLAUDE.md.bak' -> claude.
    for prefix, owner, kind in rules:
        if kind == "exact" and target.startswith(fold(prefix)):
            return owner

    return str(contract["fallback_owner"])


def main() -> None:
    try:
        payload = json.load(sys.stdin)
    except Exception:
        sys.exit(0)  # 페이로드 파싱 불가 -> 대상 경로를 알 수 없다. 권한 흐름에 맡긴다.

    tool = payload.get("tool_name", "")
    if tool not in ("Write", "Edit", "NotebookEdit"):
        sys.exit(0)

    ti = payload.get("tool_input") or {}
    target = ti.get("file_path") or ti.get("notebook_path") or ""
    if not target:
        sys.exit(0)

    root = repo_root()
    # realpath — 심볼릭 링크로 저장소 밖을 경유해 안으로 들어오는 경로를 정규화한다.
    abs_target = os.path.realpath(os.path.abspath(target))

    # ── 1. 소유권 ────────────────────────────────────────────────
    try:
        rel = os.path.relpath(abs_target, root).replace("\\", "/")
    except ValueError:  # 다른 드라이브
        rel = None

    inside_repo = rel is not None and not rel.startswith("../") and rel != ".."
    if inside_repo:
        rules, contract = load_policy(root)

        # [4] 정책 파일 자체는 Claude 도 쓰지 않는다. 자기 경계를 무승인으로 넓히는 경로를 막는다.
        fold = (lambda s: s) if contract["case_sensitive"] else (lambda s: s.casefold())
        if fold(rel) == fold(POLICY_REL):
            deny(
                f"[정책 자기수정 차단] '{rel}' 은 3-AI 소유권의 단일 소스입니다.\n"
                f"→ Claude 가 자기 경계를 정의하는 파일을 스스로 고치면 경계가 의미를 잃습니다.\n"
                f"→ 경계를 바꿔야 한다면 사람에게 변경안과 사유를 제시하고 승인받아 사람이 반영합니다.\n"
                f"→ 참고: docs/STATUS-AUDIT-2026-08-24.md §2-⑤"
            )

        owner = owner_of(rel, rules, contract)
        if owner not in ALLOWED_OWNERS:
            deny(
                f"[역할 경계] '{rel}' 의 소유자는 '{owner}' 입니다. Claude 는 include/**, docs/**, "
                f"config/**, .claude/**, Cogito++_*.md 만 씁니다.\n"
                f"→ 파일을 고치지 말고 체크리스트 §1-2 형식의 이슈로 {owner} 에게 인계하십시오.\n"
                f"→ 경계 자체를 바꿔야 한다면 scripts/ownership-policy.json 을 먼저 사람 승인 아래 수정합니다."
            )

    # ── 2. 제어문자 ──────────────────────────────────────────────
    base = os.path.basename(abs_target)
    ext = os.path.splitext(base)[1].lower()
    if ext in TEXT_EXT or base in TEXT_BASENAME:
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
