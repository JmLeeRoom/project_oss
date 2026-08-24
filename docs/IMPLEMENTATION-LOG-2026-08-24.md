# 실행 기록 — STATUS-AUDIT-2026-08-24 §3-C Claude 레인

```text
실행:        Claude (계약 관리자 · 감사관)
일자:        2026-08-24
근거:        docs/STATUS-AUDIT-2026-08-24.md §3-C(C1~C8), §2-④
기준 HEAD:   6998321  (변경 없음 — 커밋하지 않았다)
쓰기 범위:   include/**, docs/**, .claude/**  — 전부 Claude 소유
커밋/푸시:   없음. git 쓰기는 하지 않았다
```

---

## 1. 산출물

### 신설 (12)

| 파일 | 내용 |
| --- | --- |
| `include/cogito/canonical_json.hpp` | CCJ v1 정규 직렬화. `ccj::Json`·`ParseStrict`·`Serialize`·`SelfTest`·`ParseLimits` |
| `include/cogito/tool.hpp` | `Effect`·`Risk`·`Idempotency`·`ToolStatus`·`ToolResultStatus`·`ToolResult`·`ToolHandler`·`ToolDescriptor`·`ValidateToolContract` |
| `include/cogito/permit.hpp` | `ExecutionPermit` — 불변식 1을 타입으로 강제 |
| `include/cogito/tool_schema.hpp` | `GrammarCoverage`·`SchemaAudit`·`CompiledSchema`·`SchemaCompiler` **(계획 외 — §3-①)** |
| `docs/g0/G0-10-regex-timeout.md` | pattern 200ms 상한 정정안 (Proposed) |
| `docs/g0/G0-18-20-web-contract.md` | 세션·CSRF / body schema·오류 envelope·command cache / `assets_digest` (Proposed) |
| `docs/g0/G0-21-storage-migration.md` | SQLite 마이그레이션·WAL 임계·보존/백업/복구 (Proposed) |
| `docs/g0/G0-LEDGER.md` | G0-01~33 추적표 + G0 Exit Gate 6개 체크 실측 |
| `docs/g0/G0-17-QUESTIONNAIRE.md` | 제품 책임자 결정표 (G0-13·14·16·17·22) |
| `docs/approvals/APPROVAL-REQUEST-001-G0-RESOLUTION-9.md` | G0 9건 결재 요청 (1장) |
| `docs/handoff/HO-codex-002.md` | Codex 인계 (X0~X5) |
| `docs/handoff/HO-antigravity-002.md` | Antigravity 인계 (G1~G11 + minor 5) |
| `.claude/hooks/.gitignore` | `__pycache__/`·`*.py[cod]` |

### 수정 (10)

| 파일 | 내용 |
| --- | --- |
| `.claude/hooks/cc_guard.py` | fail-open→fail-closed, 대소문자 우회 차단, 파일/디렉터리 prefix 구분, 정책 자기수정 차단, 2단 매칭 |
| `include/cogito/fsm.hpp` | 선반영 고지 + **명세 충돌 3행 명시** + `Timeout` 사상 미결 |
| `include/cogito/{result,ids,invoker,ops_log,context_compactor}.hpp` | 선반영 고지 |
| `include/cogito/ops_log.hpp` | `Error()` → `LogError()` (타입 가림 제거) |
| `docs/handoff/HO-codex-001.md` | C1~C6 실측 상태 반영 |
| `docs/prompts/TASK_PROMPT_CODEX.md` | 전면 개정 (상위 문서 충돌 5건) |

---

## 2. 검증 — 실행한 명령과 출력

### 2-1. 헤더 (10개)

```
$ g++ -std=c++17 -Wall -Wextra -Wpedantic -fsyntax-only -I include -I <stub> tu_<each>.cpp
  개별 10/10 통과, 동시 include OK, 경고 0
```

> ⚠ **`<stub>` 단서** — `canonical_json.hpp` 가 `<nlohmann/json.hpp>` 를 include 한다.
> 이는 결함이 아니라 명세가 규정한 설계다(`§4-3:450`·`:455` `using Json = nlohmann::json;`,
> `§9:2413` `PUBLIC nlohmann_json::nlohmann_json`).
> 그러나 **저장소에 nlohmann 이 없다** — 벤더링 사본 없음, `vcpkg.json` 없음, 툴체인에도 없음.
> 따라서 이 통과는 **최소 stub 을 저장소 밖 스크래치에 주입한 결과**이며 실물로 재현된 것이 아니다.
> "10개 헤더가 컴파일된다" 를 stub 단서 없이 인용하지 마라.
> 의존성 획득 경로(`vcpkg.json` 또는 벤더링)는 Codex 소유다 → `HO-codex-002` X4.

행동 검증 (실제 실행):

```
grammar_coverage raw = 2   (Full=0 Partial=1 None=2)
기본값이 None 인가?      YES (명세 §4-4:578 준수)
effect 기본 Destructive? YES
risk 기본 Critical?      YES
idempotency 기본 Unsafe? YES
approval_required 기본?  true
OpsLogger 파생에서 Error 를 타입으로 사용: OK
```

### 2-2. 가드 (`cc_guard.py`)

```
소유권 결정   22/22 통과   (대소문자 우회 4 · 형제 경로 회귀 4 · 정책 자기수정 2 포함)
fail-closed    8/8 통과   (파일 삭제 · JSON 파손 · rules 공백 · 배열 · 미지원 algorithm
                           · BOM 정상 · 무관 필드 추가 시 교차DoS 없음)
```

### 2-3. 소유권 자체 감사

```
$ git log --oneline -1
6998321 update            ← 변경 없음

Claude/shared 소유 변경: 24건
타 소유자 경로 변경    : 11건  ← 전부 이번 작업 이전부터 존재
    M antigravity  .agents/rules/**, .agents/skills/**, tests/web/**, tools/**   (10건)
   ?? codex        .codex/CODEX_NEXT_STEPS_2026-08-24.md                          (1건)
```

---

## 3. 계획을 벗어난 판단 3건 — 사후 승인 대상

### ① `tool_schema.hpp` 신설 (계획에 없던 4번째 헤더)

**왜** — 검증에서 blocking 이 나왔다. `ToolDescriptor::grammar_coverage` 가 값 초기화(`{}`)로
`GrammarCoverage::Full` — **가장 관대한 값** — 이 됐다. 명세 `§4-4:578` 은 `None` 을 규정한다.
`Full` 이면 `§8-5 [A-3]` 문법 커버리지 경고가 영원히 뜨지 않는다.

`GrammarCoverage` 는 `§2:67` 상 `tool_schema.hpp` 담당이라 `tool.hpp` 에 정의하면 레이아웃이 갈라진다.
`§4-6(:660-699)` 이 `tool_schema.hpp` 전문을 이미 규정하고 있어 그대로 옮겼다.
**결과: 레이아웃을 지키면서 blocking 해소.** 실측으로 `None`(raw=2) 확인.

### ② 선반영 고지를 3개가 아니라 **10개 헤더 전부**에

**왜** — 기존 6개 헤더도 전부 `G0 결정 :` 줄에서 `G0-RESOLUTION-9`(Proposed)를 인용한다.
신규 3개에만 고지를 붙이면 **고지 없는 헤더는 규범인 것처럼 읽힌다.**
`fsm.hpp` 에는 추가로 명세와의 **실제 충돌 3행**을 표로 명시했다.

### ③ `ops_log.hpp` 의 `Error()` → `LogError()` 이름 변경

**왜** — 멤버 함수 `Error` 가 클래스 유효범위에서 타입 `cogito::Error` 를 가린다.
`OpsLogger` 를 상속한 클래스는 본문에서 `Error Flush();` 를 쓸 수 없다
(`error: 'Error' does not name a type`). 헤더 집합만으로는 드러나지 않고
**구현을 시작하는 순간 반드시 깨진다.** 구현체가 아직 없는 지금이 가장 싸다.

> ⚠ 이는 **기존 공개 계약의 변경**이다. 되돌리려면 지금 말해달라.

---

## 4. 실행 중 발견한 신규 사항

### 4-1. 🔴 자기 개정에서 회귀를 만들었다 — 검증이 잡았다

`cc_guard.py` 초판 개정이 파일 rule 을 startswith → 정확일치로 좁히면서,
이전에 codex 로 매칭되어 deny 되던 형제 경로가 어떤 rule 에도 안 걸려 `shared` 로 떨어졌다.

```
실측:  AGENTS.md.bak · CMakeLists.txt.bak · vcpkg.json.tmp  →  ALLOW 로 뒤집힘
```

게다가 파일 머리 주석에 *"모두 가드를 좁히는 방향이며 새로 허용되는 경로는 없다"* 라고
**단언까지 써놨다.** 2단 매칭으로 되돌리고 주석의 거짓 단언을 정정했다.
**자기 개정이 안전 방향이라는 주장도 실측해야 한다는 사례로 남긴다.**

### 4-2. 🟠 `ToolResultStatus::Timeout` 의 FSM 사상이 규범 어디에도 없다

```
명세 :540              ToolResultStatus 에 Timeout 정의됨
명세 :963-964 (§4-10)  Execute 를 떠나는 이벤트는 ExecOk / ExecErrorOrIndeterminate 둘뿐
명세 :979              보편 규칙 서술은 취소 두 경우만 다룸
ADR-0001               "Timeout" grep 0 hit
```

`ExecOk` 로 메우면 **타임아웃이 성공으로 기록된다.** 지어내지 않고 `fsm.hpp` R3 에 미결로 명시했다.
`effect != None` 인 타임아웃은 애초에 `Indeterminate` 이므로 미결 범위는 `effect == None` 하나다.

### 4-3. 🟠 헤더 간 줄번호 인용이 이미 썩었다

선반영 고지를 추가하며 `invoker.hpp` 가 5줄 밀렸고, `tool.hpp`·`permit.hpp` 의 인용 12건이
한꺼번에 어긋났다. 두 검증 레인의 정정값조차 1~2줄 달랐다.
→ **헤더 상호참조를 심볼 앵커로 전환했다**
(`invoker.hpp ToolInvoker::Invoke [실행 계약] 4` 형식). 남은 헤더 간 줄번호 인용 **0건**.

### 4-4. ⚪ Codex 소유 파일 5개의 mtime 이 갱신됐다 (내용 변경 0)

`2026-08-24 10:13:38` 에 9ms 안에 5개가 묶여 갱신됐다 — `.codex/hooks/cogito_hooks.py`,
`AGENTS.md`, `.agents/skills/cogito-stage-owner/{SKILL.md,references/source-map.md}`,
`.codex/hooks/tests/test_cogito_hooks.py`.
**`git status` 에 안 잡히므로 내용은 동일하다.** 롤백할 것은 없다.
단발 수기 편집이 아니라 일괄 쓰기의 흔적이므로 **Codex 에게 주체 확인이 필요하다** → `HO-codex-002`.

### 4-5. ⚪ `cc_guard` 는 Bash 경유 쓰기를 막지 못한다

`Write`/`Edit`/`NotebookEdit` 세 툴만 검사한다. 리다이렉션·heredoc·`cp`·`python -c` 는 전부 우회한다.
차단은 오탐 비용이 크므로 **감사 로그 경로**를 대안으로 파일 머리에 미결로 기록했다.

---

## 5. 하지 않은 것 — 그리고 그 이유

| 항목 | 이유 |
| --- | --- |
| **커밋 · 브랜치 생성 · `.gitignore` 루트 생성 · `git rm --cached`** | git 쓰기. §3-A-①② 사람 승인 대상 |
| **명세 본문 정정** (`Cogito++_구현명세서.md`) | `G0-RESOLUTION-9.md:9` — *"승인 전에는 명세 본문을 고치지 않는다"*. §4-② |
| **ADR 0001·0004 를 Accepted 로 전환** | `ADR-0004:233` 이 Accepted 조건을 3-컴파일러 바이트 비교로 걸었는데 빌드 시스템이 없다. §4-③ |
| **`AGENTS.md` · `.codex/**` Sanitizer 정정 (X1)** | Codex 소유 → `HO-codex-002` |
| **`guard-scope.ps1` 가드 결함 (G10)** | `shared` 경로 + Antigravity 어댑터 → `HO-antigravity-002` |
| **Antigravity 산출물 커밋 (G1)** | 소유자가 아니며 git 쓰기 → `HO-antigravity-002` |
| **G0-17 현장값** | `G0-RESOLUTION-9.md:697-698` — *"AI 가 정할 수 없다"*. 질문지만 작성 |
| **빈 디렉터리 `.exe`·`2`·`1787302872804955000` 삭제** | 저장소 스크립트 원인 아님이 실측 배제됨. 사람 판정 |
| **`docs/traceability.md`** | G0 결정이 확정돼야 요구사항→명세→ADR→작업→테스트 연결을 채울 수 있다. 승인 후 |

---

## 6. 다음 차단점

**사람 판정 8건**(`STATUS-AUDIT §3-A`)이 그대로 남아 있고, 그중 **①②③** 이 나머지를 막는다.

```
①  .chrome_test_profile/ 추적 해제 + .gitignore 신설 승인
②  Antigravity 산출물을 antigravity/* 브랜치로 분리 커밋 승인    ← ① 보다 먼저 실행해야 안전
③  G0-RESOLUTION-9 승인  (권고 순서 ④ → ② → ⑦ → 나머지)
    결재 문서: docs/approvals/APPROVAL-REQUEST-001-G0-RESOLUTION-9.md
```

승인이 떨어지면 Claude 의 다음 작업은 **`Cogito++_구현명세서.md` 본문 정정**이다
(체크리스트가 아니다 — `:112` 가 명세를 대상으로 규정한다).
