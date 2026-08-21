# HO-codex-001 — 가드레일 계층 무력화 및 소유권 규칙 중복 6건

```text
Task ID:       HO-codex-001
원본 근거:     scripts/ownership-policy.json, CLAUDE.md §2·§4, Cogito++_개발_작업체크리스트.md §2-3
선행 작업:     scripts/ownership-policy.json 개정 (.codex/, AGENTS.md, CLAUDE.md 추가, 2026-08-21)
변경 파일:     .codex/hooks/**, .codex/hooks.json, .codex/stage-state.json, AGENTS.md, .codex/agents/**
비범위:        include/**, docs/**, config/**, tools/web_*/**  (Claude/Antigravity 소유)
검토자:        Claude (계약) → 사람 (승인)
차단 여부:     blocking 2건 / major 3건 / minor 1건
```

---

## 🚨 C1 — 가드레일이 지금 이 순간 작동하지 않는다 (blocking)

**실측 확인 (2026-08-21):**

```
$ ls .codex/hooks/
.gitignore   __pycache__/   tests/          ← cogito_hooks.py 가 없다

$ .codex/hooks.json 이 참조하는 대상
  -> /.codex/hooks/cogito_hooks.py
  -> cogito_hooks.py
```

`__pycache__` 가 남아 있으므로 **한 번 로드된 뒤 삭제**됐다. `hooks.json` 은 5개 이벤트 전부에서
여전히 이 파일을 호출한다. **Codex 의 소유권·커밋·push·파괴적 git 게이트가 전부 무력 상태다.**

**조치**: `cogito_hooks.py` 를 복구하거나(권장), 복구할 수 없으면 `hooks.json` 을 비활성화하고
가드레일이 없다는 사실을 명시적으로 보고하라. **조용히 계속하는 것이 가장 나쁘다.**

---

## 🚨 C2 — `stage-state.json` 자기 승인 (blocking)

Codex 의 G0 게이트·커밋·push·파괴적 git 권한이 전부 `.codex/stage-state.json` 을 읽어 결정된다.
그런데 이 파일을 보호하는 가드가 없었다.

**부분 완화 완료** — `scripts/ownership-policy.json` 에 `.codex/ → codex` 를 추가했고,
`scripts/guard-scope.ps1` 을 정책 파일을 읽도록 고쳐 **Antigravity 는 이제 차단**된다(실측 확인).
Claude 쪽 `cc_guard.py` 도 `.codex/` 를 codex 소유로 보므로 차단된다.

**남은 문제**: **Codex 자신이 자기 게이트 상태를 쓸 수 있다.** `g0_gate` 를 `passed` 로
스스로 바꿀 수 있으면 체크리스트 §2-3 Exit Gate 가 의미를 잃는다.

**사람 판정 필요** — `.codex/stage-state.json` 을 human-write-only 로 뺄 것인가.
판정 전까지는 이 파일 변경을 커밋에 반드시 노출하고 리뷰 대상으로 삼기를 권한다.

---

## C3 — 소유권 규칙을 Python 상수로 복제 (major)

`cogito_hooks.py` 가 `scripts/ownership-policy.json` 을 읽지 않고 규칙을 코드에 복제했다.

**세 가드가 각자 규칙을 복제하면 반드시 갈라진다.** 실제로 갈라졌다:
Antigravity 가드는 4개 패턴만 검사해 명세서·ADR·`config/policy.json`·`.claude/settings.json` 을
전부 allow 했다(실측 확인 후 수정 완료).

**조치**: `cogito_hooks.py` 복구 시 `scripts/ownership-policy.json` 을 **읽어서** 판정하라.
참고 구현이 두 개 있다 — `.claude/hooks/cc_guard.py`(Python), `scripts/guard-scope.ps1`(PowerShell).
둘 다 "가장 긴 prefix 우선, 정책 파일 없으면 fail-closed" 로 동일하게 동작한다.

BOM 주의: 정책 파일을 읽을 때 `encoding="utf-8-sig"` 를 쓰라.

---

## C4 — `.agents/skills/cogito-stage-owner/**` 를 단일 소스 밖에서 카브아웃 (major)

`AGENTS.md` 와 Codex 훅이 이 경로를 Antigravity 소유에서 떼어내 Codex 쪽으로 돌렸는데,
`scripts/ownership-policy.json` 에는 그 예외가 없다. 정책상 `.agents/` 는 antigravity 다.

**조치**: 카브아웃이 필요하면 **정책 파일을 먼저 고쳐라**(사람 승인 아래). 코드에서 우회하지 말 것.
필요 없으면 `AGENTS.md` 와 훅에서 예외를 제거하라.

---

## C5 — Sanitizer 적용 단계가 체크리스트와 다르다 (major)

`AGENTS.md` 와 stage-owner 스킬이 ASan/UBSan/TSan 을 **"S4~S6"** 으로 한정한다.
체크리스트는 **S1, S6, S7, S9, S10, S11** 에서 요구하고 **S4·S5 에는 요구하지 않는다.**
겹치는 구간이 S6 하나뿐이다.

**조치**: 체크리스트를 권위로 삼아 정정하라. 특히 **S1(결정론 기반)의 fuzz + ASan/UBSan** 이
빠지면 CCJ·digest 의 sanitizer 검증이 통째로 누락된다.

---

## C6 — stage-state 스키마에 없는 필드를 쓰라고 지시 (minor)

stage-owner 스킬이 스키마와 훅 어디에도 없는 필드를 기록하라고 한다.
스키마를 확장하거나 스킬에서 제거하라. 둘 중 하나로 정렬하면 된다.

---

## 참고 — 사람 판정 대기 중인 항목 (Codex 작업에 영향)

1. **문서 권위 순서 충돌**: `AGENTS.md` 는 `구현_요구사항` > `개발_작업체크리스트`,
   `CLAUDE.md` 는 그 반대다. 두 에이전트가 서로 다른 결론에 도달한 뒤에야 충돌을 알게 된다.
   판정 후 **양쪽 다 로컬 목록을 지우고 한 곳을 가리키게** 하는 것을 권한다.
   (`AGENTS.md` 는 검증보고서를 "approved" 라 부르는데, 그 문서에 승인 표시가 없다는 점도 함께 판정 필요)
2. **S10 선행 개발 승인 여부**: ADR 0009·위협모델·`tests/web/auth`·S7 모두 없는 상태에서 시작됐다.
3. **`.codex/stage-state.json` human-write-only 여부** (C2).

## 알림 — G0 자기모순 9건 확정안이 나왔다

`docs/g0/G0-RESOLUTION-9.md` 와 `docs/adr/0001`·`0004` 가 `Proposed` 상태다. 승인되면:

- **ABI 는 v1.1 단일 기준** (`MAJOR=1 MINOR=1`). v1.0 시그니처는 구현하지 않는다
- 모든 상태변경 함수가 `const cogito_subject_t*` 를 받는다
- `COGITO_ERR_WRONG_THREAD = 26` 신설, 공개 함수 전부에 `@thread` 주석 필수
- `domain::kOperation` 과 `operation_digest` 신설
- CCJ 골든표 1행 정정 (`2^53+2` → `9007199254740994`)
- **숫자 직렬화는 로케일 독립 필수.** 전역 `setlocale` 의존 금지.
  `to_chars(general, P)` 와 `_l` snprintf 가 같은 바이트를 내는지 **3-컴파일러 비교로 판정**해 보고할 것

승인 전에는 S1 제품 코드에 착수하지 않는다(체크리스트 §2-3).
