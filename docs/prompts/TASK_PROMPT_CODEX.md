# [Codex Task] Cogito++ S0 — 저장소 골격과 빌드 기반

```text
수신자:     Codex (C++ 코어 · 빌드 구현 엔진)
발신자:     Claude (계약 관리자 · 감사관)
개정:       2026-08-24 — 전면 개정 (개정 사유는 아래 §0)
권위:       Cogito++_개발_작업체크리스트.md §4 (S0-01 ~ S0-07, :144-262)
상태:       ⛔ 착수 차단 — G0 Exit Gate 미충족
```

---

## 0. ⚠ 이 지시서는 2026-08-24 에 전면 개정됐다

**이전 판을 그대로 실행하면 안 된다.** 상위 문서와 충돌하는 지시가 5건 있었다.

| # | 이전 판의 지시 | 실제 권위 | 근거 |
| --- | --- | --- | --- |
| 1 | vcpkg 의존성 `nlohmann-json-schema-validator` (2.1.0+) | **`json-schema-validator` `2.4.0`** — 포트명이 존재하지 않는 이름이었다 | 체크리스트 `:193`. `CLAUDE.md §3-1` 이 이 이름을 **가짜 포트명 전례로 명시**하고 있다 |
| 2 | preset 6종 = `linux-debug`, `linux-release`, `linux-asan`, `linux-tsan`, `win-msvc-debug`, `win-msvc-release` | **`windows-msvc-debug`, `linux-gcc-debug`, `linux-clang-asan`, `linux-clang-tsan`, `linux-release`, `linux-arm64-release`** — 6개 중 1개만 일치했다 | 체크리스트 `:198` |
| 3 | Codex 가 `include/cogito/`, `config/` 를 생성하고 `result.hpp`·`cogito.h` 를 배치 | **둘 다 Claude 소유다.** Codex 는 이 경로에 쓰지 않는다 | `scripts/ownership-policy.json:20,22`, `AGENTS.md:51` |
| 4 | S0-03 ~ S0-06 을 한 번에 구현 | **정확히 한 티켓만 선택한다** | `AGENTS.md:26` — *"Select exactly one ticket such as `S1-05`; never work on an entire stage implicitly"* |
| 5 | Exit Gate = 경고 0건 + ctest 100% | **S0 Exit Gate 는 5개 조항이다**(§4 참조). 축소하면 통과가 아니다 | 체크리스트 `:256-262` |

> 이 개정은 `docs/handoff/HO-antigravity-001.md` 가 진단한 것과 **같은 패턴의 재발을 막는 것**이다.
> 그 티켓의 32건은 대부분 "지시서가 시킨 대로 만든 결과"였고, **원인은 구현이 아니라 지시서였다.**

---

## 1. ⛔ 지금은 착수할 수 없다 — 먼저 읽을 것

체크리스트 `:120` **G0 Exit Gate**:

> **G0 Exit Gate**: 위 6개 체크가 모두 완료되고 안전 차단 0건일 때만 **S0** 제품 코드 작업을 시작한다.

**S1 이 아니라 S0 이 막혀 있다.** 이 지시서의 전 범위가 그 게이트 뒤에 있다.

### 6개 체크의 현재 상태 (2026-08-24 실측)

| 체크 (체크리스트 `:109-114`) | 상태 |
| --- | --- |
| G0-01~G0-33 각각에 owner·결정일·ADR/이슈 링크 배정 | ❌ — `docs/g0/G0-LEDGER.md` 로 추적 시작, 배정 미완 |
| 모든 "전체/안전/감사 차단" 항목이 `Resolved` | ❌ — **`Resolved` 0건.** 9건이 `Proposed`(승인 대기), 24건 미착수 |
| 보류 항목의 기본 OFF·배포 제외 확인 | ❌ |
| 결정 결과를 `Cogito++_구현명세서.md` 또는 승인된 ADR 에 역반영 | ❌ — 승인 전이므로 **의도적 미반영** |
| `docs/traceability.md` 작성 | ❌ — 파일 없음 |
| 아키텍트·안전·보안·제품 책임자 승인 | ❌ — `docs/approvals/APPROVAL-REQUEST-001-G0-RESOLUTION-9.md` 결재 대기 |

### 그래서 지금 할 일

**S0 제품 파일을 만들지 마라.** `.codex/stage-state.json` 이 이미 이 상태를 반영하고 있다
(`phase=idle`, `g0_gate=not_passed`, `allowed_write_paths=[]`).

대신 착수 전 가능한 것은 별도 인계서에 있다 → **`docs/handoff/HO-codex-002.md`**

---

## 2. 소유권 — 이 지시서가 허가하는 쓰기 범위

`scripts/ownership-policy.json` 이 단일 소스다. Codex 제품 쓰기는 **활성 티켓의 allowlist 부분집합**에 한한다.

```
쓰기 가능 : src/**  tests/**(단 tests/web/** 제외)  cmake/**
            tools/cli/**  tools/web_host/**  bindings/**
            CMakeLists.txt  CMakePresets.json  vcpkg.json  vcpkg-configuration.json
제어 평면 : AGENTS.md  .codex/**  .agents/skills/cogito-stage-owner/**

쓰기 금지 : include/**  docs/**  config/**  .claude/**  CLAUDE.md  Cogito++_*.md   ← Claude
            tools/web_dashboard/**  tools/mock_server/**  tests/web/**  .agents/**  ← Antigravity
공유(승인) : scripts/**  README.md  루트 task prompt
```

**`include/cogito/**` 와 `config/**` 는 Claude 소유다.** 헤더가 없거나 부족하면
**파일을 만들지 말고** 체크리스트 §1-2 형식의 인계 이슈로 Claude 에게 올려라(`AGENTS.md:53`).

### 루트 파일의 소유 공백 — 사람 판정 필요

`.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`, `.clang-tidy`, `LICENSE`,
`NOTICE`, `THIRD_PARTY_LICENSES.md`, `SECURITY.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md` 는
S0-03 이 요구하지만 **`ownership-policy.json` 의 어떤 rule 에도 없어 `shared` 로 떨어진다.**
`shared` 는 *"정확한 경로에 대한 사용자 승인 후에만 쓰기 가능"* 이다.

→ **S0-03 착수 전에 이 목록의 소유자를 사람에게 판정받아라.** 임의로 쓰지 마라.

---

## 3. 티켓 — 하나씩. 순서대로

`AGENTS.md:22-34` **Start protocol** 을 매 티켓마다 수행한다. 요약:

1. 티켓 **하나**를 고른다 (예: `S0-03`).
2. 체크리스트 §1·§2·§3, 해당 티켓과 Exit Gate, §17·§18·§19 를 읽는다.
3. 대응하는 요구사항·구현명세서 절과 참조된 모든 ADR·계약을 읽는다.
4. **직전 Exit Gate 와 적용되는 모든 G0 결정에 증거가 있는지 확인한다.**
5. `contract_auditor` 서브에이전트를 돌린다. **`PASS` 일 때만 계속한다.**
6. `.codex/stage-state.json` 에 티켓·게이트 상태·쓰기 allowlist·검증 명령·증거 디렉터리를 기록한다.
7. `codex/<ticket>-<slug>` 브랜치를 만들거나 전환한다. **`main` 에서 제품 작업을 커밋하지 않는다.**

구현 티켓에는 `$cogito-stage-owner` 를 쓴다.

### 티켓 목록과 권위 위치

| 티켓 | 내용 | 체크리스트 |
| --- | --- | --- |
| **S0-01** | 릴리스 범위·불변식 13개를 `docs/architecture-invariants.md` 에 번호로 고정 | `:148-166` |
| **S0-02** | ADR 템플릿·상태 정의, ADR 0001~0008 매핑 | `:168-183` |
| **S0-03** | 저장소 골격, 루트 위생 파일, generated 파일의 source-of-truth 주석 | `:185-194` |
| **S0-04** | Toolchain·preset 6종 고정 | `:196-203` |
| **S0-05** | vcpkg 공급망 고정, SBOM seed | `:205-217` |
| **S0-06** | root `CMakeLists.txt`, Catch2 v3 harness, install-consumer smoke | `:229-239` |
| **S0-07** | CI 골격과 추적성 | `:245-254` |

> ⚠ **S0-01 과 S0-02 는 산출물이 `docs/**` 다 — Claude 소유다.**
> Codex 는 이 두 티켓의 **파일을 쓰지 않는다.** 내용을 Claude 에게 인계하거나,
> 사람이 소유권을 조정한 뒤에 착수한다. 이 충돌 자체를 사람에게 보고하라.

---

## 4. 반드시 정확히 쓸 값 — 지어내지 마라

`AGENTS.md:69`: *"Never invent a missing port name, version, target name, API signature,
license, timeout, or security default. Verify version-sensitive facts in first-party sources
and record the source."*

### 4-1. vcpkg (S0-05, 체크리스트 `:205-217`)

```
nlohmann-json            3.11.3          ← override
json-schema-validator    2.4.0           ← override.  "nlohmann-" 접두어가 붙지 않는다
```

- `builtin-baseline` 을 **실제 40자리 commit SHA** 로 고정한다. 자리표시자를 남기지 마라.
- feature 는 `audit` · `http` · `http-curl` · `opcua` · `mqtt` · `rag` · `tests` · `web` 을 **각각 독립 해석**한다.
- 다음 포트·feature 이름을 **baseline 에서 재검증**하고 결과를 기록한다:
  `open62541[openssl]`, `cpp-httplib[openssl]`, `curl[ssl]`, `paho-mqttpp3`, SQLite `fts5`/`json1`.
- `picosha2.h` 는 원본 URL · commit/tag · SHA-256 · MIT 고지를 기록하고 파일 내 저작권을 보존한다.
- **G0-11**(web 자기 의존 표현)이 미해소다. 해소 전에는 그 부분을 확정하지 마라.

검증(체크리스트 `:219-224`):

```powershell
vcpkg x-update-baseline --dry-run
vcpkg install --dry-run --x-manifest-root=.
vcpkg install --dry-run --x-manifest-root=. --x-feature=tests --x-feature=http --x-feature=opcua
```

### 4-2. Preset 6종 (S0-04, 체크리스트 `:198`)

```
windows-msvc-debug
linux-gcc-debug
linux-clang-asan
linux-clang-tsan
linux-release
linux-arm64-release
```

- **ASan/UBSan 과 TSan 은 서로 호환되지 않는다. 별도 preset 으로 둔다**(`:203`).
- single-config / multi-config preset 의 build directory 가 충돌하지 않게 분리한다(`:199`).
- warning-as-error 는 **프로젝트 코드에만** 적용한다. 외부 헤더에 적용하지 않는다(`:200`).
- 경고 플래그는 `CogitoWarnings.cmake` 에 모은다 —
  MSVC `/permissive- /W4`, GCC/Clang `-Wall -Wextra -Wpedantic`(`:201`).
- **Release 에서도 assertions 에 의존하지 않는 런타임 검사를 유지한다**(`:202`).
  이는 `include/cogito/result.hpp:158-160` 의 G0-09 R4(`std::terminate`)와 같은 계약이다.
- 공통값: C++17, extensions **OFF**, PIC, hidden visibility(`:197`).
- 최소 컴파일러: MSVC 2019 16.11+ / GCC 9+ / Clang 12+ — **또는 G0-22 결정으로 갱신**(`:197`).
  G0-22 는 *"AI 가 정할 수 없다"* 로 분류된 항목이다(`docs/g0/G0-RESOLUTION-9.md:697-698`).
  사람 결정 전에는 이 값을 확정으로 쓰지 마라.

### 4-3. 툴체인 실측 (2026-08-24)

```
cmake   NOT FOUND        ninja   NOT FOUND        vcpkg   NOT FOUND
clang   NOT FOUND        cl      NOT FOUND
gcc     /c/msys64/ucrt64/bin/gcc  (유일)
```

**S0 Exit Gate 의 "모든 preset 이 smoke configure/build/test 를 통과한다" 는 현재 환경에서
실행 자체가 불가능하다.** 게이트가 열려도 **환경 준비가 선행**한다.
이 사실을 사람에게 먼저 보고하고, 어느 환경에서 6개 preset 을 실증할지 합의하라.
설치 못 한 preset 을 "통과"로 기록하지 마라 — `AGENTS.md:91`.

---

## 5. Sanitizer 적용 단계 — 정정 필요

`AGENTS.md:72` 와 `.agents/skills/cogito-stage-owner/SKILL.md:77` 이 **"S4 through S6"** 으로 한정한다.
체크리스트의 실제 요구는 **S1 · S6 · S7 · S9 · S10 · S11** 이고 **S4·S5 에는 요구하지 않는다.**
겹치는 구간이 S6 하나뿐이다.

특히 **S1-07**(`:332-357`)의 fuzz + ASan/UBSan 이 빠지면
**CCJ v1 · LP digest 의 sanitizer 검증이 통째로 누락된다**(`:341`, `:353`).

→ 두 파일 모두 **Codex 소유**다(`ownership-policy.json:28,40`). **지금 바로 정정할 수 있다.**
단계마다 요구되는 sanitizer 종류가 다르므로 "모든 단계에 ASan/UBSan/TSan 셋 다"로 단순화하지 마라.

---

## 6. 완료 기준 — S0 Exit Gate (체크리스트 `:256-262`)

**5개 전부**여야 한다. 하나라도 빠지면 통과가 아니다.

- [ ] **G0 차단 0건이다.**
- [ ] 모든 preset 이 최소 smoke configure/build/test 를 통과한다.
- [ ] 고정 baseline 으로 **네트워크 없는 재설치**가 가능하다.
- [ ] **ADR 0001~0008 이 승인 상태다.** (현재 0001·0004 만 존재하고 둘 다 `Proposed`)
- [ ] **설치 소비자 smoke 가 source tree 밖에서 성공한다.**
      (`find_package(cogito CONFIG REQUIRED)` — 체크리스트 `:239`)

### 티켓 단위 완료 규약 (`AGENTS.md:83-92`)

1. 티켓의 **정확한 명령**과 영향받는 회귀 테스트를 실행한다.
2. `git diff --check` 를 돌리고 **변경·미추적 경로를 전부 확인한다.**
3. 명령·종료코드·툴체인 버전·테스트 출력·sanitizer 결과·안전 증거·마스킹 결과를 체크리스트 §18 아래 보존한다.
4. `exit_gate_verifier` 를 돌린다. **`PASS` 이고 티켓 수용 조항마다 증거가 있을 때만** 수용한다.
5. **한 티켓의 PASS 를 단계 PASS 로 승격하지 마라.** 명시적 단계 종료 작업이 아니면
   `STAGE_EXIT_GATE: NOT_EVALUATED` 로 기록한다.
6. `.codex/stage-state.json` 의 verifier 상태는 **독립 검증 이후에만** `pass` 로 바꾼다.
7. **실제 명령과 결과 수치를 보고한다. 대응하는 출력 없이 "verified" 라고 쓰지 마라.**

Codex 는 사용자가 명시적으로 요청하지 않는 한 **commit·push·tag·merge·sign·PR 을 하지 않는다.**

---

## 7. 알아둘 것 — 저장소의 현재 상태

- **워킹트리에 Antigravity 의 미커밋 변경 1,567줄이 있다**(`tools/**`, `tests/web/**`, `.agents/**`).
  `AGENTS.md:7` 이 금지한 형태다 — *"Never run another writer inside the Codex worktree."*
  **`git clean`·`git reset`·`git checkout` 을 쓰지 마라. 남의 산출물이 날아간다.**
- **`.chrome_test_profile/` 930개 파일이 추적 중이고 `.gitignore` 가 없다.**
  `git status` 노이즈의 원인이다. 사람 승인 아래 정리 대기 중이다.
  **당신의 커밋에 이 경로가 딸려 들어가지 않게 하라.**
- 저장소 루트에 빈 디렉터리 `.exe`, `2`, `1787302872804955000` 이 있다. 셸 리다이렉션 사고 흔적이다.
  **저장소 스크립트가 원인이 아님은 실측으로 배제됐다.** 삭제는 사람 판정 대기.

전체 실측 근거: **`docs/STATUS-AUDIT-2026-08-24.md`**
