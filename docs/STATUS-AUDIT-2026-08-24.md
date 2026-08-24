# 현황 검증 및 즉시 조치서 — 2026-08-24

```text
작성:        Claude (계약 관리자 · 감사관)
기준 시각:   2026-08-24 (Asia/Seoul)
기준 브랜치: codex/ops-bootstrap
기준 HEAD:   6998321 "update"
검증 방법:   6개 레인 병렬 실측 + 각 결론 적대적 반증 (에이전트 52, 도구 호출 951)
검증 대상:   사람이 제시한 "3-Agent 현황 요약 및 Next Steps" 문서의 모든 주장
판정:        요약문은 2026-08-21 시점 자료로는 대체로 맞으나,
             2026-08-24 실측 기준으로 6건이 사실과 다르고 4건은 지시 자체가 틀렸다.
관련 문서:   .codex/CODEX_NEXT_STEPS_2026-08-24.md (Codex가 같은 날 독립 작성, 미추적)
```

> **읽는 법** — §1은 요약문 주장의 진위, §2는 요약문에 아예 없던 차단 사항, §3은 지금 당장 할 일,
> §4는 착수하면 안 되는 일이다. **§3-A(사람)가 §3-C(각 AI)의 대부분을 막고 있다.**

## 📌 실행 상태 — 2026-08-24 갱신

**§3-C 의 Claude 레인(C1~C9)은 전부 실행됐다.** 실행 기록: `docs/IMPLEMENTATION-LOG-2026-08-24.md`

| 구간 | 상태 |
| --- | --- |
| §3-A 사람 판정 8건 | ⛔ **전부 대기 중.** 이게 나머지를 막는다. 결재 문서: `docs/approvals/APPROVAL-REQUEST-001-G0-RESOLUTION-9.md` |
| §3-B 저장소 정리 | ⛔ **미수행.** git 쓰기이므로 사람만 가능 |
| §3-C 📘 Claude C1~C9 | ✅ **완료.** 헤더 4종 신설 + G0 문서 5종 + 인계 2종. 커밋은 하지 않았다 |
| §3-C 🛠️ Codex X1~X4 | 📤 **인계 완료** → `docs/handoff/HO-codex-002.md` (Codex 소유 경로라 Claude 가 쓰지 않음) |
| §3-C 🎨 Antigravity G1~G11 | 📤 **인계 완료** → `docs/handoff/HO-antigravity-002.md` |

> **§2-④ 가드 결함은 Claude 쪽만 닫혔다.** `cc_guard.py` 는 수정·검증 완료(소유권 22/22, fail-closed 8/8).
> `guard-scope.ps1`(Antigravity, `shared` 경로)은 **미수정** — 사람 승인 후 Antigravity 가 수행한다.

---

## 0. 한 줄 결론

**요약문이 "다음 스텝"이라 부른 Claude 작업 3건 중 2건은 지금 착수하면 계약을 깨뜨린다.
그리고 요약문에 없는 두 건 — 저장소 위생과 미커밋 산출물 — 이 그보다 먼저다.**

세 AI의 실제 상태는 이렇다.

| AI | 지금 할 수 있는 일 | 근거 |
| --- | --- | --- |
| **Antigravity** | **있다. 즉시.** 1,567줄 산출물을 커밋만 하면 된다 | 워킹트리에 완성돼 있고 소유 경로 안이다 |
| **Claude** | **부분적.** 헤더 2종 + 잔여 G0 초안은 가능, 명세 정정은 불가 | 명세 정정은 사람 승인이 전제 |
| **Codex** | **없다.** 사람 판정 없이는 한 줄도 못 쓴다 | `.codex/stage-state.json`: `phase=idle`, `g0_gate=not_passed`, `allowed_write_paths=[]` |

---

## 1. 요약문 주장별 판정

### 1-1. 사실과 다른 것 (6건)

| # | 요약문 주장 | 실측 판정 | 근거 |
| --- | --- | --- | --- |
| ① | Codex가 **"S0 CMake/vcpkg 골격"** 을 만들었다 | **거짓** | `CMakeLists.txt`·`CMakePresets.json`·`vcpkg*.json`·`cmake/`·`src/` 전부 부재. `git rev-list --all --reflog` 전수 검색 결과 **어떤 커밋에도 한 번도 존재한 적 없음**. 4개 ref(main, origin/main, codex/ops-bootstrap, origin/…) 전부 동일. 저장소 전체 `.cpp`/`.cmake` 파일 **0개** |
| ② | Codex 가드레일 **복구가 필요하다** (HO-codex-001 C1) | **옛 정보** | `.codex/hooks/cogito_hooks.py`(2,257줄) 커밋 6998321에 실재. `hooks.json` 5개 이벤트 전부 이 파일을 가리키고, `ownership-policy.json`을 **실제로 읽어** longest-prefix 판정하며 정책 로드 실패 시 fail-closed(실행 검증). **C1·C3(복제)·C4는 해소됨** |
| ③ | Antigravity가 **32건 정정 작업을 해야 한다** | **대부분 이미 완료 — 단 전량 미커밋** | §12-5 경로 **15/15 정확 일치**(HEAD는 0/15), `server.on('upgrade')`→400 거부(`server.js:643`), SSE(`id:`/`Last-Event-ID`/15초 keepalive/503 상한) 구현, `innerHTML` **0건**, BOM 6개 제거 + `guard-scope.ps1` BOM 정확히 보존 |
| ④ | `.agents` 자산에 **구형 WebSocket/경로 표기가 남아있다** (F10) | **워킹트리에서는 거짓** | 3개 파일 모두 SSE·15경로·W6 거부로 개정 완료. **단 미커밋** |
| ⑤ | G0 미해소 동안 **S1 착수 금지** | **원문은 S0** | `Cogito++_개발_작업체크리스트.md:120` — *"위 6개 체크가 모두 완료되고 안전 차단 0건일 때만 **S0** 제품 코드 작업을 시작한다"*. `CLAUDE.md`와 `HO-codex-001:118`이 각각 "S1"으로 잘못 의역 |
| ⑥ | Step 2-1: **체크리스트 본문**에 정정 diff 반영 | **대상 문서가 틀림** | `G0-RESOLUTION-9.md`의 `[명세 수정 diff]` 6개는 **전부 `Cogito++_구현명세서.md`** 의 절(§8-2·§8-4·§6-4·§4-12·§6-1·§3-4·§4-1·§4-10·§5·§4-7·§6-2·§4-8)을 대상으로 함. 체크리스트 대상 diff **0건**. 체크리스트 §2-3 자신도 *"결정 결과를 `Cogito++_구현명세서.md` 후속 버전 또는 승인된 ADR에 역반영"* 이라 규정 |

> **⑥이 왜 심각한가** — `CLAUDE.md §4` 우선순위는 `승인된 ADR > 구현명세서 > 체크리스트`다.
> 상위 문서를 안 고치고 하위 문서만 고치면 **충돌 시 고치지 않은 구현명세서가 이긴다.**
> 정정이 무효화될 뿐 아니라, 두 문서가 서로 다른 ABI 시그니처·CCJ 규칙을 갖게 되어
> G0-01/G0-23이 해소가 아니라 **확대 재생산**된다. Codex는 §4 규칙대로 구현명세서를 읽는다.

### 1-2. 맞는 것

- **소유권 체계** — `scripts/ownership-policy.json`이 단일 소스인 것, 3자 배분의 큰 틀은 정확.
  (보완: Codex는 `tools/cli/**`·`tools/web_host/**`·`bindings/**`도 소유하고,
  `.agents/skills/cogito-stage-owner/**`는 `.agents/` 안의 Codex 카브아웃이다.)
- **G0 9건 정정안 + ADR 0001·0004 작성 완료, 사람 승인 대기** — 정확.
  세 문서 모두 `상태: Proposed`(각 `:3`, `G0-RESOLUTION-9.md:9`).
- **projection 함수 9개 / 도메인 태그 9개 / C ABI 헤더가 미작성** — 정확.
  `include/` 아래 `.h` 파일 0개, `cogito_subject_t`는 명세서 본문에만 존재.
- **G0-10 / G0-17~20 / G0-21이 미해소** — 번호·내용 6개 인용 전부 실제 대장과 일치
  (체크리스트 `:82`, `:89`~`:93`).

### 1-3. 표현을 고쳐야 하는 것

| 요약문 표현 | 정정 표현 | 이유 |
| --- | --- | --- |
| "핵심 C++ 헤더 6종 작성 완료" | "명세 §2의 **24개 중 6개** 작성, 18개 미작성" | `구현명세서:60-84`가 `include/cogito/` 아래 24개 헤더를 규정 |
| "G0 자기모순 9건 확정안" | "**작성 9 / 승인 0 / 미착수 24**" | Proposed를 '해소'로 세면 §2-3 Exit Gate가 무력화됨 |
| "가드레일 복구" | "**파일 복구 완료, 회귀 테스트 red(24/93 실패)**" | 아래 §2-② |
| "HO-antigravity-001 32건" | 표의 실제 ID 합계는 **41개**(A1~F11+G) | 티켓 자체의 카운트 불일치. Claude가 정정해야 함 |

---

## 2. 요약문에 없는 차단 사항 (7건)

### ① 🔴 저장소가 리뷰 불가 상태 — 다른 모든 작업의 전제

```
추적 파일 999개 중 930개(62.3MB / 63.3MB, 98.4%)가 .chrome_test_profile/
.gitignore 파일 자체가 없음
GitHub 공개 저장소 github.com/JmLeeRoom/project_oss, 브랜치 codex/ops-bootstrap 에 푸시 완료
```

**자격증명 실피해는 없다 — 이 점은 정확히 보고한다.** 직접 SQLite로 열어 확인한 결과:

| 파일 | 실측 |
| --- | --- |
| `Default/Login Data` | `logins` 테이블 **0행** |
| `Default/Network/Cookies` | `cookies` 테이블 **0행** |
| `Default/History` | `urls`/`visits` **0행** |
| `Local State` | DPAPI 래핑된 `os_crypt.encrypted_key` 존재 — **해당 Windows 계정에 바인딩**되어 타인이 복호화 불가 |

**진짜 피해는 리뷰 모델의 붕괴다.** `git diff --stat`이 61개 파일을 내는데 그중 **55개가 브라우저 캐시 바이너리**다.
3-AI 협업의 안전 장치는 전부 "diff를 사람이 본다"를 전제하는데, 그 전제가 지금 성립하지 않는다.
이번 감사에서도 실제 산출물을 보려면 grep으로 노이즈를 걸러내야 했다.
Antigravity가 브라우저 QA를 한 번 돌릴 때마다 저장소가 다시 오염된다.

### ② 🔴 가드레일 자체 회귀 테스트가 red — 24/93 실패

```
$ python .codex/hooks/tests/test_cogito_hooks.py
Ran 93 tests in 4.875s
FAILED (failures=24)      ← 2회 반복 실행 동일. AssertionError 24건, ImportError 0건
```

원인은 플레이키가 아니라 **죽은 코드**다. `cogito_hooks.py:1210-1219`의 무조건 git 차단이
그 아래 `:1382-1519`의 stage-state 기반 인가 로직 약 300줄을 **도달 불가능**하게 만들었다.
실제 동작은 테스트가 가정한 계약보다 **더 엄격**해서 보안상 위험하진 않지만,
**사람이 명시적으로 커밋을 승인해도 훅이 거부하므로 Codex는 어떤 경로로도 커밋할 수 없다.**
이것이 의도인지 사고인지 코드만으로는 판별 불가하다 → 사람 판정 필요(§3-A-⑤).

### ③ 🔴 Antigravity 산출물 1,567줄이 커밋 없이 Codex 브랜치 워킹트리에 떠 있음

`git checkout`·`git reset` 한 번, 혹은 위 ①을 정리하려는 `git clean` 한 번으로 **전부 소실된다.**
게다가 `AGENTS.md:7`이 명시적으로 금지한 형태다 —
*"Never run another writer inside the Codex worktree."*
Codex가 자기 브랜치를 커밋하면 남의 산출물을 자기 커밋에 끌어들인다.

### ④ 🔴 가드 3종이 `guard_contract`를 서로 다르게 구현 — 실측 우회 성공

`ownership-policy.json`의 `guard_contract`(longest_prefix / **case_sensitive: false** / separator / special_root_prefixes / fallback)를
실제로 구현한 것은 `.codex/hooks/cogito_hooks.py` **하나뿐**이다.
`cc_guard.py`와 `guard-scope.ps1`은 `guard_contract` 키를 **한 번도 참조하지 않는다**(grep 0회).

```
실측:  Claude 가드에 SRC/foo.cpp           → allow   (deny 여야 함)
       Antigravity 가드에 Include/cogito/fsm.hpp → allow   (deny 여야 함)
```

**NTFS는 대소문자를 구분하지 않는다.** 즉 `SRC/foo.cpp`는 `src/foo.cpp`와 같은 파일이다.
Claude는 대문자 경로로 Codex의 `src/`·`tests/`를, Antigravity는 `Include/`·`Docs/`로
Claude의 헤더 계약과 ADR을 각각 우회해 덮어쓸 수 있다. **가드가 있다는 전제 자체가 무너진다.**

추가로 `cc_guard.py:112`는 정책 파일이 없을 때 **경고만 하고 통과시킨다(fail-open)** —
다른 두 가드는 fail-closed다. 정책 파일 하나만 지우면 Claude의 경계가 통째로 사라진다.

### ⑤ 🟠 `ownership-policy.json` 자신이 무승인 수정 가능

정책 파일은 어떤 rule에도 없어 `shared`로 떨어진다. Claude와 Antigravity는 **자기 경계를 정의하는 파일을
승인 없이 다시 쓸 수 있다.** 가드의 deny 메시지가 *"정책 파일을 사람 승인 아래 먼저 수정하라"* 고
안내하는데, 그 수정 자체가 무승인으로 가능하므로 **우회 절차를 친절히 알려주는 꼴**이다.

### ⑥ 🟠 커밋된 헤더가 현행 규범 명세를 위반

`include/cogito/fsm.hpp:73-98`은 이미 **R0~R4**를 전문 반영했고 `Idle`을 R2에서 제거했으며
`ResetForTestOnly`(`:154`)로 격리했다. 그런데 `Cogito++_구현명세서.md`는 아직 `R1/R2/R3`이고
`:972-974`의 R2에 `Idle`이 남아 있으며 `:1003`은 `ResetForNextTurn()` 직접 대입이다
(`grep R0/R4` → 명세서 본문 **0 hit**).

ADR-0001이 `Proposed`이므로 **현재 권위는 명세다.** 즉 커밋된 헤더가 규범을 위반한 상태로 저장소에 있다.
**Codex가 헤더를 보고 구현하면 명세 위반, 명세를 보고 구현하면 헤더 위반 — 어느 쪽도 통과할 수 없다.**

### ⑦ 🟠 `TASK_PROMPT_CODEX.md`에 가짜 vcpkg 포트명이 살아있음

`CLAUDE.md §3-1`이 *"사실 주장은 1차 자료 확인 후에만 쓴다"* 의 **전례로 명시한 바로 그 이름** —
`nlohmann-json-schema-validator`(실제는 `json-schema-validator`) — 이 Codex 지시서에 그대로 남아 있다.
`HO-antigravity-001`이 진단한 "**원인은 지시서였다**" 패턴의 재발 대기 상태다.

---

## 3. 지금 당장 해야 할 일

### 3-A. 🔴 사람만 할 수 있는 일 (8건) — 이게 나머지를 막고 있다

이 8건은 AI가 대신할 수 없다. **①②③이 가장 급하고, 나머지는 병렬로 판정 가능하다.**

| # | 판정 사항 | 왜 지금인가 | 결정하면 풀리는 것 |
| --- | --- | --- | --- |
| **①** | **`.chrome_test_profile/` 추적 해제 + `.gitignore` 신설 승인** | 커밋이 5개뿐인 **지금이 히스토리 재작성이 가장 싼 시점**. 브라우저 QA를 한 번 더 돌리면 더 커진다 | §2-① 전체. 이후 모든 diff 리뷰 |
| **②** | **Antigravity 산출물을 `antigravity/*` 브랜치로 분리 커밋 승인** | `git clean` 한 번에 1,567줄 소실. ①의 정리 작업이 정확히 그 위험을 부른다 | §2-③. Claude/Codex의 교차 리뷰 착수 |
| **③** | **G0-RESOLUTION-9 승인 — 권고 순서 `④ → ② → ⑦ → 나머지`** | ②④⑦은 **되돌림 불가**. 승인 없이 명세를 고치면 되돌림 불가 결정 3건이 사람 판단을 우회해 확정됨 | Claude의 명세 정정 착수. S0 게이트 |
| ④ | **문서 권위 순서 판정** — `CLAUDE.md §4` vs `AGENTS.md` Authority order (실제로 서로 반대) | 이미 사고가 났다. Sanitizer 단계 불일치(S4~S6 vs S1·S6·S7·S9·S10·S11)가 정확히 이 충돌의 산물 | Codex의 Sanitizer 정정. 향후 모든 충돌 |
| ⑤ | **`cogito_hooks.py` 무조건 git 차단이 의도인가 사고인가** | 의도면 죽은 코드 300줄 + 테스트 24건을 지우고, 사고면 조건을 인가 로직 뒤로 옮긴다. **코드만으로는 판별 불가** | §2-②. Codex의 커밋 경로 |
| ⑥ | **`.codex/stage-state.json` human-write-only 분리 여부** | Codex가 자기 G0 게이트를 스스로 `passed`로 바꿀 수 있다. 지금 값이 안전한 건 우연이지 구조가 아니다 | §2-③ 자기승인. `needs_human_ruling` 1건 |
| ⑦ | **`artifacts/` 소유자** | 아직 안 생겼으므로 **지금이 가장 싸다.** 생긴 뒤엔 소급 정리가 필요 | `needs_human_ruling` 나머지 1건 |
| ⑧ | **진행 중인 Web 작업이 "선행 프로토타입"인가 "S10 제품 코드"인가** (HO-antigravity-001 F11) | S10 착수 조건 3개(ADR 0009 Accepted / `docs/web-threat-model.md` / G0-17~20·33 해소)가 **전부 미충족인데 산출물은 이미 있다.** 제품이면 §12-9 스택(Vite+React18+Tailwind+shadcn/ui)을 맞춰야 하고, 프로토타입이면 S10 종료 증거로 인용할 수 없다 | Antigravity의 다음 단계 전체 |

> **④의 부수 판정** — `AGENTS.md:15`가 `Cogito++_구현_요구사항_검증보고서.md`를 "approved"라 부르는데
> 그 문서에는 승인 표시가 없고 스스로 "조건부 가능"이라 결론짓는다. 승인 헤더를 붙이거나 "approved"를 지워야 한다.

### 3-B. 🟠 저장소 정리 — ①② 승인 직후, 이 순서로

순서를 지켜야 한다. **뒤집으면 산출물이 날아간다.**

```bash
# 1) Antigravity 산출물을 먼저 안전하게 만든다  ← 이걸 먼저 하지 않으면 3)에서 소실
git switch -c antigravity/s10-ho001
git add tools/web_dashboard tools/mock_server tests/web .agents
git commit          # .chrome_test_profile 은 절대 스테이징하지 않는다

# 2) .gitignore 신설  (shared 경로 → 사람 승인 필요)
#    최소:  .chrome_test_profile/   __pycache__/   *.py[cod]   node_modules/   artifacts/

# 3) 추적 해제
git rm -r --cached .chrome_test_profile

# 4) 히스토리에서 완전 제거 — 커밋 5개인 지금이 가장 싸다
#    git filter-repo 등. 원격 재작성이므로 사람 승인 필수
```

**부수 정리** — 저장소 루트의 빈 디렉터리 `.exe`, `2`, `1787302872804955000` 삭제.
저장소 안 스크립트가 원인이 아님은 실측으로 배제됐다(리다이렉션 패턴 0건).
외부 셸에서 `<cmd>.exe … 2> <나노초타임스탬프>` 형태의 인자가 잘못 전달된 흔적으로 보인다.
git이 빈 디렉터리를 모르므로 `status`·`diff` 어디에도 안 잡힌다.

### 3-C. 각 AI의 즉시 착수 항목

#### 📘 Claude — 승인 없이도 지금 가능한 것

| 순위 | 작업 | 비고 |
| --- | --- | --- |
| **C1** | **`docs/approvals/` 경로·형식 합의 + 승인 요청서 1장 작성** | `G0-RESOLUTION-9.md:686-694` '승인 요청 사항' 표 + `:683` 권고 순서 기준. **3-A-③을 푸는 열쇠이므로 최우선** |
| **C2** | **`include/cogito/canonical_json.hpp` · `permit.hpp` · `tool.hpp` 신설** | `fsm.hpp`·`invoker.hpp`가 이 셋을 include하는데 **셋 다 없어 전처리조차 통과 못 한다.** 지금 Codex는 헤더를 받아도 착수 불가. 승인 불필요(§2-⑥과 무관한 신규 계약) |
| **C3** | **`HO-codex-001.md` C1·C3·C4 상태 갱신** | 해소된 blocking을 blocking으로 남겨두면 다음 세션이 **훅을 끄는** 정반대 사고를 낸다. `docs/`는 Claude 소유 |
| **C4** | **G0 대장 추적표 신설** — 33건 × (상태 / owner / 결정일 / ADR·이슈 링크) | 체크리스트 `:109` 요구사항인데 표에 열 자체가 없어 추적 불가. 진척은 항상 `작성 9 / 승인 0 / 미착수 24` 형식으로 |
| **C5** | **`fsm.hpp` 상단에 선반영 고지 추가** | *"이 파일은 G0-RESOLUTION-9 / ADR-0001 Proposed를 선반영한 초안이며 승인 전 규범이 아니다"*. §2-⑥의 최소 완화 |
| **C6** | **잔여 G0 정정안 — AI 착수 가능분만** | **가능: G0-10, G0-18, G0-19, G0-20, G0-21.** `G0-21`은 `adr-draft/SKILL.md:16`이 ADR-0004 담당으로 매핑했으나 실제 0004는 CCJ·digest·해시체인만 커버 → **조용한 커버리지 공백**. 먼저 정정할 것 |
| **C7** | **G0-17은 질문지만** | `G0-RESOLUTION-9.md:697-698`이 `G0-13`·`G0-14`·**`G0-17`(인증원)**·`G0-22`·`G0-16`을 *"AI가 정할 수 없다"* 로 묶는다. 정상 산출물은 "선택지표 + 제품 책임자 결정 필요"다. **현장값(인증원·승인 역할·SOD 예외)을 추정으로 채우면 안 된다** — `CLAUDE.md §3-1`이 지목한 실패 유형 |
| **C8** | **`TASK_PROMPT_CODEX.md`의 `nlohmann-json-schema-validator` 정정** | §2-⑦ |

#### 🛠️ Codex — 사람 판정(3-A-④⑤⑥) 전까지 제품 코드 착수 불가. 그동안 가능한 것

| 순위 | 작업 | 비고 |
| --- | --- | --- |
| **X1** | **`AGENTS.md:72` · `cogito-stage-owner/SKILL.md:77`의 `S4 through S6` → `S1, S6, S7, S9, S10, S11`** | 두 파일 모두 Codex 소유(`ownership-policy.json:28,40`)라 **지금 바로 가능**. 특히 **S1-07의 fuzz + ASan/UBSan**이 빠지면 CCJ·digest의 sanitizer 검증이 통째로 누락된다 |
| **X2** | **`cogito_hooks.py:135, :523, :1871`의 `encoding="utf-8"` → `"utf-8-sig"`** | 누군가 PowerShell로 정책 파일을 한 번 저장하면 BOM이 붙고 **Codex 훅이 전건 deny로 잠긴다**(안전하지만 전면 중단) |
| **X3** | 훅 테스트 24건 — **3-A-⑤ 판정 후** 한쪽으로 정렬 | 판정 전엔 손대지 말 것. 어느 쪽이 권위인지가 결정 사항이다 |
| **X4** | **S0 착수 전 툴체인 확인** | 실측: `cmake`·`ninja`·`vcpkg`·`clang`·`cl` **전부 미설치**(gcc만 msys64에 존재). S0 Exit Gate의 6-preset smoke는 **현재 환경에서 실행 자체가 불가능**하다. 승인이 나도 환경 준비가 선행 |

#### 🎨 Antigravity — 지금 즉시 가능. 커밋이 1순위

| 순위 | 작업 | 비고 |
| --- | --- | --- |
| **G1** | **3-B의 1) 커밋** | 다른 모든 것보다 먼저 |
| **G2** | **거짓 통과 출력 제거** | `csp-and-hmi.test.js:395`가 `web/csrf: VERIFIED (CSRF headers & Origin check on REST)`를 출력하는데 **server.js에 CSRF 검증도 Origin 검사도 없다.** `audit_readpath`도 동일. HO F4의 "거짓 통과"가 **문구만 바뀐 채 살아있다** → `NOT IMPLEMENTED`로 정정 |
| **G3** | **`index.html:58` 주석 정정** | *"Driven strictly from /api/transitions"* 라 써 있으나 FSM 그래프는 여전히 **하드코딩 SVG**다(F2 미해소) |
| **G4** | **F5 — write 0회 단언 완성** | 현재 **위조 경로 1개뿐**. 미승인·만료 경로 단언이 없다. `§11 S10`·불변식 8의 **프로젝트 최우선 수용 기준** |
| **G5** | **F7 — `npm test` 실행 가능하게** | 스크립트는 생겼으나 cwd 상대경로 때문에 실제 실행 불가. **실행되지 않는 테스트는 통과가 아니다** |
| **G6** | **CSRF·Origin·content-type 3중 검사 구현** | POST 7경로 공통 전처리. G2의 정정이 아니라 **해소** |
| **G7** | **`command_id` — 7개 POST 전부** | 현재 6/7이 미수신. `/api/session/seal`은 **되돌릴 수 없는 조작인데 멱등 방어가 0** |
| **G8** | **`process_epoch_id` 재생 금지** | 프로세스 재시작 후 seq가 1부터 다시 매겨지면 브라우저의 `Last-Event-ID` 재생이 **다른 epoch의 감사 이벤트를 잘못 이어붙여** 존재하지 않는 이력을 운영자 화면에 만든다 |
| **G9** | 승인 모달에 `requester_subject_id`·`approval_required` 2셀 추가 | 없으면 야간 교대 운영자가 W12 자기승인 분리를 육안 검증할 수 없다 |
| **G10** | **G8 이후**: 3-A-⑧ 판정 대기 | 제품 판정이면 §12-9 스택 전환 |

---

## 4. 지금 착수하면 안 되는 일 (요약문의 잘못된 지시 4건)

| # | 요약문 지시 | 정정 | 근거 |
| --- | --- | --- | --- |
| **①** | Step 2-1 — **체크리스트 본문**에 정정 diff 반영 | **대상은 `Cogito++_구현명세서.md`.** 체크리스트는 diff 대상이 아니라 §2-2 G0 표의 상태·owner·링크 갱신 대상일 뿐. **두 작업을 분리 기술할 것** | §1-1-⑥ |
| **②** | Step 2-1 — 승인 후 즉시 명세 정정 | **승인이 아직 없다.** `G0-RESOLUTION-9.md:9` `Proposed — 사람 승인 대기`. 승인 없이 고치면 보고서 자신의 규약을 Claude가 위반하고 **되돌림 불가 결정 3건이 사람 판단을 우회**한다 | 3-A-③ |
| **③** | Step 2-1 — ADR 0004를 **Accepted로 전환** | **사람 승인만으로 불가능하다.** `ADR-0004:233`이 스스로 Accepted 조건을 *"canonical/* 테스트가 3개 컴파일러에서 바이트 동일"* 로 걸어놨는데 `src/`·`CMakeLists.txt`·`vcpkg.json`이 전부 없어 그 테스트를 **돌릴 수 없다.** → `Proposed → (승인됨, S1 Exit Gate 대기)` 중간 상태로 둘 것. ADR-0001도 미결 2건(`kVerdictTtlNs` 구체값, `Observe`에서의 AuditError 순서)이 남아 승인 요청서에 함께 올려야 함 | `CLAUDE.md §3-2` — *"'확인했다'고 쓰지 않는다. 실행한 명령과 출력을 붙인다"* |
| **④** | Step 2-2 — **`cogito/projection.hpp`** 신설 | **파일명이 틀렸다. `include/cogito/digest.hpp`가 맞다.** `G0-RESOLUTION-9.md`가 `:167`과 `:493` 두 곳에서 명시적으로 지목. `projection.hpp`로 빼면 **같은 digest 계약이 두 헤더로 갈라지고**, `:495`의 *"projection은 단일 serializer로 구현하고 호출부에서 필드를 조립하지 않는다"* 라는 G0-26 확정 취지와 정반대가 된다. G0-26은 **되돌림 불가**라 배치 실수의 비용이 크다 | §1-2 |

**정정된 Step 2-2** — `include/cogito/digest.hpp` **한 파일**에:
도메인 태그 9개 상수 + projection 함수 9개(`action`·`operation`·`permit`·`audit`·`toolschema`·`registry`·`policy`·`config`·`model`) + LP 인코딩 규약.
`include/cogito/cogito.h`(C ABI)는 별도 — `COGITO_ABI_VERSION_MAJOR/MINOR = 1/1`,
`cogito_subject_t`(`struct_size` 포함), `COGITO_ERR_WRONG_THREAD = 26`(26은 비어 있어 재배정 협의 불필요),
공개 함수 전부에 `@thread` 주석(`구현명세서:2512`의 헤더 검사 대상).

---

## 5. 검증 방법과 한계

**방법** — 6개 레인(Codex / Antigravity / Claude 계약 / 가드레일 / 명세 정합성 / 다음스텝 감사)을
병렬 실측하고, `blocking`·`major` 판정 전건을 **독립 에이전트가 반증**하도록 했다.
에이전트 52개, 도구 호출 951회. 반증에서 **3건이 뒤집혔고** 본문에 반영했다:

| 뒤집힌 판정 | 원래 | 정정 | 사유 |
| --- | --- | --- | --- |
| Antigravity C2(승인자 유도) | PARTIAL | **TRUE(해소)** | `TASK_PROMPT_ANTIGRAVITY.md:106`이 *"mock은 고정 주체를 서버 측에서 부여한다"* 고 **지시**했다. `actor_id` 하드코딩은 미달이 아니라 지시된 구현 |
| 명세 §4-10 R0~R4 | FALSE | **PARTIAL** | 명세서에는 없지만 `fsm.hpp:73-98`에 **이미 있다**. 검증 범위가 `.md`에만 한정됐던 것 → §2-⑥의 근거가 됨 |
| G0-17 착수 가능성 | PARTIAL | **TRUE** | `:697-698` 원문은 *"AI가 **정할 수 없다**"*(결정권)이지 "산출물을 낼 수 없다"가 아니다. `g0-resolve/SKILL.md:55`·`adr-draft/SKILL.md:86-87`이 "제품 책임자 결정 필요 + 선택지 정리" 형태를 이미 규정 |

**한계 — 이 문서가 하지 않은 것**

- **읽기 전용 감사다.** 어떤 파일도 수정·커밋하지 않았다. 이 문서 자체(`docs/`, Claude 소유)만 신규 작성했다.
- **mock 서버를 실제로 기동하지 않았다**(부작용 회피). Antigravity 판정은 정적 분석 기준이다.
- **빌드를 시도하지 않았다.** 툴체인 부재로 불가능하다(X4).
- **Antigravity 관련 판정은 시점 의존적이다** — 워킹트리 기준이므로 커밋 후 재확인이 필요하다.
- **GitHub 저장소가 공개(Public)임은 확인했으나**, 이 저장소가 공개여야 하는지는 판단하지 않았다.
  공개가 의도라면 §2-①은 위생 문제로 남고, 비공개가 의도였다면 별건의 사람 판정이 필요하다.
