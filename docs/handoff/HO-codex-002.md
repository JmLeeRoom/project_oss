# HO-codex-002 — 가드레일 회귀 테스트 red · Sanitizer 단계 정정 · S0 착수 전 정리

```text
Task ID:       HO-codex-002
원본 근거:     docs/STATUS-AUDIT-2026-08-24.md §2-② §3-C(X1~X4), HO-codex-001 C3·C5
선행 작업:     HO-codex-001 (C1·C4 해소 확인, C2·C5 미해소)
변경 파일:     .codex/hooks/cogito_hooks.py, .codex/hooks/tests/**, .codex/rules/safety.rules,
               AGENTS.md, .agents/skills/cogito-stage-owner/SKILL.md
비범위:        include/**, docs/**, config/**, .claude/**, Cogito++_*.md   (Claude 소유)
               tools/web_dashboard/**, tools/mock_server/**, tests/web/**, .agents/(위 1건 제외)  (Antigravity)
               src/**, cmake/**, CMakeLists.txt, vcpkg*.json                (S0 — G0 게이트 뒤)
검토자:        Claude (계약) → 사람 (승인)
차단 여부:     blocking 1건(사람 판정 선행) / major 2건 / minor 2건
```

---

## ⛔ 먼저 — S0 제품 코드는 아직 열리지 않았다

체크리스트 `:120` **G0 Exit Gate**:

> 위 6개 체크가 모두 완료되고 안전 차단 0건일 때만 **S0** 제품 코드 작업을 시작한다.

**S1 이 아니라 S0 이 막혀 있다.** 6개 체크 전부 미충족이다(`docs/g0/G0-LEDGER.md` 참조).
`.codex/stage-state.json` 이 이미 이 상태를 반영한다 —
`phase=idle`, `g0_gate=not_passed`, `allowed_write_paths=[]`.

**아래 X1~X5 는 전부 제품 코드가 아니라 제어 평면·문서 작업이므로 게이트와 무관하게 착수 가능하다.**

---

## 🚨 X0 — 사람 판정이 선행해야 하는 것 (blocking)

**`cogito_hooks.py` 의 무조건 git 차단이 의도인가 사고인가.**

```
$ python .codex/hooks/tests/test_cogito_hooks.py
Ran 93 tests in 4.875s
FAILED (failures=24)      ← 2회 반복 실행 동일. AssertionError 24건, ImportError 0건
```

실패 24건은 전부 `ShellGuardTests` 다 —
`test_allows_commit_only_with_explicit_reference`,
`test_authorized_push_requires_current_branch_single_refspec`,
`test_blocks_force_push_even_when_authorized`,
`test_blocks_merge_and_pull_without_authorization`,
`test_ticket_branch_creation_is_exact_and_branch_queries_stay_read_only` 등.

**원인은 플레이키가 아니라 죽은 코드다.**
`cogito_hooks.py:1210-1219` 의 무조건 git 차단이 그 아래 `:1382-1519` 의
stage-state 기반 인가 로직 약 300줄을 **도달 불가능**하게 만들었다.

- 실제 동작은 테스트가 가정한 계약보다 **더 엄격**하다 → 보안 위험은 아니다.
- 그러나 **사람이 명시적으로 커밋을 승인해도 훅이 거부한다.** Codex 는 어떤 경로로도 커밋할 수 없다.
- **이것이 의도인지 사고인지 코드만으로는 판별 불가능하다.**

### 판정 후 조치

| 판정 | 할 일 |
| --- | --- |
| **의도였다** (git 은 항상 사람이) | `:1382-1519` 와 `_allowed_git_command` 를 삭제하고, 실패 테스트 24건을 새 계약에 맞게 다시 쓴다. `.codex/rules/safety.rules` 의 commit/push/merge/restore decision 을 `prompt` → `forbidden` 으로 정정한다 |
| **사고였다** (인가 기반이 계약) | `:1214` 의 조건을 인가 로직 **뒤로** 옮긴다. 테스트 24건이 green 이 되는지 확인한다 |

**판정 전에는 이 코드에 손대지 마라.** 어느 쪽이 권위인지가 결정 사항이다.

> ⚠ 어느 쪽이든 **테스트 24건이 green 이 되기 전에는 "가드레일 복구 완료"로 보고하지 마라.**
> 정확한 표현은 **"파일 복구 완료, 회귀 테스트 red(24/93 실패)"** 다.

---

## X1 — Sanitizer 적용 단계 정정 (major · 지금 바로 가능)

`AGENTS.md:72` 와 `.agents/skills/cogito-stage-owner/SKILL.md:77` 이 **`S4 through S6`** 으로 한정한다.

**체크리스트의 실제 요구는 `S1 · S6 · S7 · S9 · S10 · S11` 이고 `S4`·`S5` 에는 요구하지 않는다.**
겹치는 구간이 `S6` 하나뿐이다.

특히 **`S1-07`**(체크리스트 `:332-357`)이 빠지는 것이 치명적이다:

- `:341` — ASan/UBSan 에서 최소 지정 시간 fuzz smoke
- `:353` — canonical fuzz corpus 에서 crash/hang/sanitizer 오류 0

이게 빠지면 **CCJ v1 · LP digest 의 sanitizer 검증이 통째로 누락된다.**
CCJ 는 감사 해시체인의 첫 소비자이고 `ADR-0004` 가 **되돌림 불가**로 표시한 영역이다.

**조치**: 두 파일 모두 **Codex 소유**(`ownership-policy.json:28,40`)이므로 즉시 정정 가능하다.
단계마다 요구되는 sanitizer 종류가 다르므로 **"모든 단계에 ASan/UBSan/TSan 셋 다"로 단순화하지 마라.**
각 단계의 체크리스트 원문을 읽고 그 단계가 실제로 요구하는 lane 만 적어라.

---

## X2 — `cogito_hooks.py` 의 `utf-8-sig` 미적용 (major · 지금 바로 가능)

`:135`, `:523`, `:1871` 이 `encoding="utf-8"` 이다. `HO-codex-001 C3` 이 명시적으로 지시한
`utf-8-sig` 가 적용되지 않았다.

**무엇이 깨지는가**: 누군가 PowerShell `Set-Content`/`Out-File` 로 `ownership-policy.json` 을
한 번 저장하면 BOM 이 붙는다. 그러면 `json.load` 가 실패하고 훅이 **PreToolUse 전건을 deny** 한다.
안전 방향이긴 하지만 결과는 **Codex 작업 전면 중단**이다(가용성 사고).

**세 가드가 같은 파일을 서로 다르게 읽는다는 사실 자체가 '단일 소스' 전제의 균열이다.**
`cc_guard.py:70` 은 이미 `utf-8-sig` 를 쓴다.

**조치**: 세 곳의 `encoding` 을 `"utf-8-sig"` 로 바꾼다.

**부수**: `GUARDED_PATHSPECS`(`:103-128`)가 여전히 경로 목록을 파이썬 상수로 갖는다.
정책 파일 `rules` 에서 파생시키거나, 용도가 다르다면 **주석으로 '정책 사본 아님'을 명시**하라.
그대로 두면 다음 감사에서 다시 '규칙 복제'로 지적된다.

---

## X3 — `.codex/rules/safety.rules` 와 실제 동작의 괴리 (minor)

`safety.rules` 는 commit/push/merge/restore 를 `prompt`(승인 가능)로 기술하는데
실제 훅은 무조건 `forbidden` 이다(X0). 모순은 아니다 — 훅이 항상 더 엄격하므로
실제 동작은 fail-closed 로 수렴하고 **안전 방향으로** 갈라진다.

문제는 **`safety.rules` 를 읽는 사람이 "prompt 하면 승인 가능"으로 오해한다**는 것이다.

**조치**: X0 판정 후 한쪽으로 정렬하라. 판정 전에는 손대지 마라.

---

## X4 — S0 착수 전 툴체인 확인 (minor · 지금 확인 가능)

2026-08-24 실측:

```
cmake   NOT FOUND        ninja   NOT FOUND        vcpkg   NOT FOUND
clang   NOT FOUND        cl      NOT FOUND
gcc     /c/msys64/ucrt64/bin/gcc   (유일)
```

**S0 Exit Gate 의 "모든 preset 이 최소 smoke configure/build/test 를 통과한다"(체크리스트 `:259`)는
현재 환경에서 실행 자체가 불가능하다.** G0 게이트가 열려도 **환경 준비가 선행**한다.

**조치**: 어느 환경에서 6개 preset(`windows-msvc-debug`, `linux-gcc-debug`, `linux-clang-asan`,
`linux-clang-tsan`, `linux-release`, `linux-arm64-release`)을 실증할지 사람과 먼저 합의하라.
설치하지 못한 preset 을 "통과"로 기록하지 마라 — `AGENTS.md:91`.

---

## X5 — 지시서 개정 확인 (minor)

`docs/prompts/TASK_PROMPT_CODEX.md` 를 **2026-08-24 에 Claude 가 전면 개정**했다.
이전 판은 상위 문서와 5건이 충돌했다(가짜 vcpkg 포트명, preset 이름 6개 중 5개 불일치,
Claude 소유 경로 침범, 티켓 묶음, 축소된 Exit Gate).

**이전 판을 캐시하고 있다면 버리고 다시 읽어라.**

---

## 참고 — 저장소 상태 (Codex 작업에 직접 영향)

- **워킹트리에 Antigravity 의 미커밋 변경 1,567줄이 있다**(`tools/**`, `tests/web/**`, `.agents/**`).
  `AGENTS.md:7` 이 금지한 형태다 — *"Never run another writer inside the Codex worktree."*
  **`git clean` · `git reset` · `git checkout` 을 실행하지 마라. 남의 산출물이 소실된다.**
- **`.chrome_test_profile/` 930개 파일이 추적 중이고 `.gitignore` 가 없다.**
  `git status` 노이즈의 원인이며 사람 승인 아래 정리 대기 중이다.
  커밋 시 이 경로가 딸려 들어가지 않게 하라.
- 현재 브랜치가 `codex/ops-bootstrap` 이고 HEAD 는 `6998321` 이다.

---

## 여전히 사람 판정 대기 중인 항목 (Codex 작업에 영향)

1. **X0** — `cogito_hooks.py` 무조건 git 차단이 의도인가 사고인가
2. **문서 권위 순서 충돌** — `AGENTS.md` Authority order 는 `구현_요구사항` 계열이 위,
   `CLAUDE.md §4` 는 `체크리스트` 가 위다. **X1(Sanitizer 단계)이 정확히 이 충돌의 산물이다.**
   부수 판정: `AGENTS.md:15` 가 `Cogito++_구현_요구사항_검증보고서.md` 를 `approved` 라 부르는데
   그 문서에는 승인 표시가 없고 스스로 "조건부 가능"이라 결론짓는다.
3. **`.codex/stage-state.json` human-write-only 여부** (HO-codex-001 C2)
4. **`artifacts/` 소유자** — 아직 디렉터리가 없으므로 지금이 가장 싸다
5. **S0-03 의 루트 위생 파일 소유자** — `.gitignore`, `.gitattributes`, `.editorconfig`,
   `.clang-format`, `.clang-tidy`, `LICENSE`, `NOTICE`, `THIRD_PARTY_LICENSES.md`,
   `SECURITY.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md` 가 어떤 rule 에도 없어 `shared` 로 떨어진다
6. **S0-01·S0-02 의 소유권 충돌** — 산출물이 `docs/**`(Claude 소유)인데 티켓은 Codex 단계에 있다

전체 실측 근거: `docs/STATUS-AUDIT-2026-08-24.md`
