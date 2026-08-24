# HO-codex-001 — 가드레일 계층 무력화 및 소유권 규칙 중복 6건

```text
Task ID:       HO-codex-001
원본 근거:     scripts/ownership-policy.json, CLAUDE.md §2·§4, Cogito++_개발_작업체크리스트.md §2-3
선행 작업:     scripts/ownership-policy.json 개정 (.codex/, AGENTS.md, CLAUDE.md 추가, 2026-08-21)
변경 파일:     .codex/hooks/**, .codex/hooks.json, .codex/stage-state.json, AGENTS.md, .codex/agents/**
비범위:        include/**, docs/**, config/**, tools/web_*/**  (Claude/Antigravity 소유)
검토자:        Claude (계약) → 사람 (승인)
차단 여부:     blocking 2건 / major 3건 / minor 1건   (기표. 현재 상태는 아래 표)
```

## 📌 상태 갱신 — 2026-08-24 실측 재확인

> ⚠ **이 티켓을 그대로 믿고 행동하지 마라.** 아래 6건 중 3건은 이미 해소됐다.
> 특히 **C1 을 근거로 `hooks.json` 을 비활성화하면 살아 있는 가드를 끄게 된다.**
>
> 원문은 이력 보존을 위해 그대로 둔다. 각 항목 머리의 상태 줄이 현재 사실이다.

| 항목 | 기표 | **현재 상태** | 실측 근거 |
| --- | --- | --- | --- |
| C1 훅 파일 소실 | blocking | ✅ **RESOLVED** (2026-08-21) | `cogito_hooks.py` 2,257줄이 커밋 `6998321` 에 실재. `hooks.json` 5개 이벤트 전부가 이 파일을 가리킴 |
| C2 stage-state 자기승인 | blocking | ⛔ **OPEN — 사람 판정 대기** | `.codex/stage-state.json` 여전히 codex 소유. 현재 값은 안전(`g0_gate=not_passed`)이나 구조적 보장은 아님 |
| C3 규칙 파이썬 복제 | major | 🟡 **PARTIAL** | 정책 파일을 **실제로 읽고** fail-closed 함(실행 검증). 단 `encoding="utf-8-sig"` 미적용(`:135`, `:523`, `:1871`), `GUARDED_PATHSPECS`(`:103-128`)에 경로 목록이 남아 있음 |
| C4 카브아웃 정책 밖 | major | ✅ **RESOLVED** | `ownership-policy.json:40` 에 반영됨. 정책·`AGENTS.md`·훅 세 곳 일치 |
| C5 Sanitizer 단계 | major | ⛔ **OPEN** | `AGENTS.md:72` · `cogito-stage-owner/SKILL.md:77` 이 아직 `S4 through S6` |
| C6 스키마 밖 필드 | minor | ⚪ **재현 안 됨** | 스키마에 없는 필드를 지시하는 곳을 찾지 못함. '현 시점 불일치 없음'으로만 기록 |

### 🆕 이 티켓에 없던 신규 발견 — `HO-codex-002` 로 이관

**가드레일 회귀 테스트가 red 다.**

```
$ python .codex/hooks/tests/test_cogito_hooks.py
Ran 93 tests in 4.875s
FAILED (failures=24)
```

원인은 플레이키가 아니라 **죽은 코드**다. `cogito_hooks.py:1210-1219` 의 무조건 git 차단이
그 아래 `:1382-1519` 의 stage-state 기반 인가 로직 약 300줄을 도달 불가능하게 만들었다.
실제 동작은 테스트가 가정한 계약보다 **더 엄격**해서 보안 위험은 아니지만,
**사람이 명시적으로 커밋을 승인해도 훅이 거부하므로 Codex 는 어떤 경로로도 커밋할 수 없다.**

따라서 **"C1 이 해소됐으니 가드레일 복구 완료"로 보고하면 안 된다.**
정확한 표현은 **"파일 복구 완료, 회귀 테스트 red(24/93 실패)"** 다.

전체 실측 근거: `docs/STATUS-AUDIT-2026-08-24.md` §2-②
후속 작업 지시: `docs/handoff/HO-codex-002.md`

---

## 🚨 C1 — 가드레일이 지금 이 순간 작동하지 않는다 (blocking)

> ### ✅ RESOLVED 2026-08-24
> `cogito_hooks.py`(2,257줄)가 커밋 `6998321` 에 존재하고 추적된다.
> `hooks.json` 의 5개 이벤트가 전부 이 파일을 가리키며, 정책 파일을 동적으로 읽어
> longest-prefix 로 판정하고 정책 로드 실패 시 deny 한다(실행 검증됨).
> **아래 원문의 조치 지시("hooks.json 을 비활성화하라")를 실행하지 마라 — 가드를 끄게 된다.**

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

> ### ⛔ OPEN 2026-08-24 — 사람 판정 대기. 이 티켓에서 유일하게 남은 blocking 이다
> `ownership-policy.json` 의 `needs_human_ruling` 2건이 그대로다.
> 현재 값은 안전하지만(`phase=idle`, `g0_gate=not_passed`, `allowed_write_paths=[]`)
> **그건 우연이지 구조적 보장이 아니다.**
> 판정 항목: (a) `.codex/stage-state.json` 을 human-write-only 로 뺄 것인가
> — 최소한 `publication`/`safety` 블록만이라도 분리 권고.
> (b) `artifacts/` 소유자 — 아직 디렉터리가 생기지 않았으므로 **지금이 가장 싸다.**

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

> ### 🟡 PARTIAL 2026-08-24
> **핵심 요구는 충족됐다** — `cogito_hooks.py` 는 `ownership-policy.json` 을 실제로 읽어 판정하고,
> 정책 로드 실패 시 fail-closed 로 deny 한다(실행 검증됨). 세 가드 중 규칙을 '복제'한 것은 이제 없다.
>
> **남은 것 2가지:**
> 1. `encoding="utf-8-sig"` 미적용 — `:135`, `:523`, `:1871` 이 `"utf-8"` 이다.
>    누군가 PowerShell 로 정책 파일을 한 번 저장하면 BOM 이 붙고 **Codex 훅이 전건 deny 로 잠긴다**
>    (안전 방향이지만 Codex 작업 전면 중단 = 가용성 사고).
> 2. `GUARDED_PATHSPECS`(`:103-128`)가 여전히 경로 목록을 파이썬 상수로 갖는다.
>    용도가 정책 사본이 아니라면 주석으로 '정책 사본 아님'을 명시하라.
>
> ⚠ **주의 — 이 항목은 이제 Codex 가 아니라 다른 두 가드의 문제가 더 크다.**
> `cc_guard.py`(Claude)와 `guard-scope.ps1`(Antigravity)은 `guard_contract` 키를
> **한 번도 참조하지 않았고**, `case_sensitive: false` 미구현으로 실측에서 우회에 성공했다
> (`SRC/foo.cpp` → allow). `cc_guard.py` 는 2026-08-24 에 수정·검증 완료(21/21 + 8/8),
> `guard-scope.ps1` 은 **미수정 — Antigravity 인계 대상**이다.
> 근거: `docs/STATUS-AUDIT-2026-08-24.md` §2-④

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

> ### ✅ RESOLVED 2026-08-24
> `scripts/ownership-policy.json:40` 에 `{"prefix": ".agents/skills/cogito-stage-owner/", "owner": "codex"}`
> 가 반영됐다. 코드 우회가 아니라 **단일 소스를 고치는** 올바른 방식으로 해소됐고,
> 정책·`AGENTS.md`·훅 세 곳이 일치한다. 추가 조치 불필요.

`AGENTS.md` 와 Codex 훅이 이 경로를 Antigravity 소유에서 떼어내 Codex 쪽으로 돌렸는데,
`scripts/ownership-policy.json` 에는 그 예외가 없다. 정책상 `.agents/` 는 antigravity 다.

**조치**: 카브아웃이 필요하면 **정책 파일을 먼저 고쳐라**(사람 승인 아래). 코드에서 우회하지 말 것.
필요 없으면 `AGENTS.md` 와 훅에서 예외를 제거하라.

---

## C5 — Sanitizer 적용 단계가 체크리스트와 다르다 (major)

> ### ⛔ OPEN 2026-08-24 — 미해소. 지금 바로 고칠 수 있다
> `AGENTS.md:72` 와 `.agents/skills/cogito-stage-owner/SKILL.md:77` 이 아직 `S4 through S6` 이다.
> **두 파일 모두 Codex 소유**(`ownership-policy.json:28,40`)이므로 사람 판정 없이 즉시 정정 가능하다.
> 단계마다 요구되는 sanitizer 종류가 다르므로 "모든 단계에 셋 다"로 단순화하지 마라.

`AGENTS.md` 와 stage-owner 스킬이 ASan/UBSan/TSan 을 **"S4~S6"** 으로 한정한다.
체크리스트는 **S1, S6, S7, S9, S10, S11** 에서 요구하고 **S4·S5 에는 요구하지 않는다.**
겹치는 구간이 S6 하나뿐이다.

**조치**: 체크리스트를 권위로 삼아 정정하라. 특히 **S1(결정론 기반)의 fuzz + ASan/UBSan** 이
빠지면 CCJ·digest 의 sanitizer 검증이 통째로 누락된다.

---

## C6 — stage-state 스키마에 없는 필드를 쓰라고 지시 (minor)

> ### ⚪ 재현 안 됨 2026-08-24
> 스키마(`.codex/stage-state.schema.json`)에 없는 필드를 기록하라고 지시하는 곳을 찾지 못했다.
> 그 사이 정렬됐거나 원 지적이 과했다. **어느 커밋에서 정렬됐는지 근거가 없으므로
> '해소'가 아니라 '현 시점 불일치 없음'으로만 기록한다.**

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
