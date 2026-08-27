# [Gemini Task] Codex TICKET-S3-FSM-001 턴 실행 상태기계 (FSM) 구현 및 결정론적 전이표 검증 (체크리스트 S3-01 ~ S3-05)

> **작성자**: Gemini (설계·계약 총괄)
> **수행자**: Codex (구현·빌드·테스트 전담)
> **기준일**: 2026-08-27
> **티켓**: **TICKET-S3-FSM-001 (체크리스트 S3-01, S3-02, S3-03, S3-04, S3-05)**
> **승인 상태**: S0, S1, S2 완료 및 승인(84/84 PASS), G0-24 Accepted(2026-08-27), ADR-0001(D1·D2·D3·D5·D6) Accepted 반영 완료 — S3 FSM 단독 착수 공식 승인

---

## 1. Objective (단일 집중 티켓 목표)

S2 단계(Tool Schema, Registry, Config/Secret)의 성공적 완료에 이어, Cogito++ 코어 엔진의 중심 상태 머신인 **FSM (`Fsm`, `State`, `Event`, `TransitionRecord`, `ToString`, `IsTerminal`, `ResolveUniversal`, `VerifyTableIntegrity`, `DumpTable`)**을 구현하고 19개 명시 전이 및 R0~R4 보편 규칙을 전수 단위 검증합니다.

---

## 2. Header & Contract Specification (Gemini 소유 공개 헤더)

Codex는 `include/cogito/**` 공개 헤더의 선언 및 계약을 100% 준수합니다:

### 2-1. 헤더 계약 목록
- [`include/cogito/fsm.hpp`](file:///C:/Users/yoonsy/Desktop/%EC%97%85%EB%AC%B4_2026/1.%20%EC%97%B0%EA%B5%AC%EC%86%8C/0.%20%ED%94%84%EB%A1%9C%EC%A0%9D%ED%8A%B8/2026/codyssey/project_oss/include/cogito/fsm.hpp):
  `State`, `Event`, `Transition`, `kTransitions`, `TransitionRecord`, `Fsm`, `ToString`, `IsTerminal`
- [`include/cogito/clock.hpp`](file:///C:/Users/yoonsy/Desktop/%EC%97%85%EB%AC%B4_2026/1.%20%EC%97%B0%EA%B5%AC%EC%86%8C/0.%20%ED%94%84%EB%A1%9C%EC%A0%9D%ED%8A%B8/2026/codyssey/project_oss/include/cogito/clock.hpp):
  `Clock`, `SystemClock`, `FakeClock` (`NowUtcRfc3339()`, `MonotonicNs()`)
- [`include/cogito/ids.hpp`](file:///C:/Users/yoonsy/Desktop/%EC%97%85%EB%AC%B4_2026/1.%20%EC%97%B0%EA%B5%AC%EC%86%8C/0.%20%ED%94%84%EB%A1%9C%EC%A0%9D%ED%8A%B8/2026/codyssey/project_oss/include/cogito/ids.hpp):
  `ActionId`, `TurnId`
- [`include/cogito/canonical_json.hpp`](file:///C:/Users/yoonsy/Desktop/%EC%97%85%EB%AC%B4_2026/1.%20%EC%97%B0%EA%B5%AC%EC%86%8C/0.%20%ED%94%84%EB%A1%9C%EC%A0%9D%ED%8A%B8/2026/codyssey/project_oss/include/cogito/canonical_json.hpp):
  `ccj::Json`
- [`include/cogito/result.hpp`](file:///C:/Users/yoonsy/Desktop/%EC%97%85%EB%AC%B4_2026/1.%20%EC%97%B0%EA%B5%AC%EC%86%8C/0.%20%ED%94%84%EB%A1%9C%EC%A0%9D%ED%8A%B8/2026/codyssey/project_oss/include/cogito/result.hpp):
  `Error`, `Errc::Internal`

---

## 3. Implementation Specification (세부 구현 규범)

### 3-1. Enum 정의, 문자열 변환 및 터미널 판정 (`ToString`, `IsTerminal`)

1. **`State` (10개 고정 상태)**:
   - `Idle`, `Infer`, `Propose`, `Gate`, `AwaitApproval`, `Execute`, `Observe`, `Done`, `Failed`, `Cancelled`
   - `ToString(State s)` 반환 문자열:
     - `State::Idle` $\rightarrow$ `"Idle"`
     - `State::Infer` $\rightarrow$ `"Infer"`
     - `State::Propose` $\rightarrow$ `"Propose"`
     - `State::Gate` $\rightarrow$ `"Gate"`
     - `State::AwaitApproval` $\rightarrow$ `"AwaitApproval"`
     - `State::Execute` $\rightarrow$ `"Execute"`
     - `State::Observe` $\rightarrow$ `"Observe"`
     - `State::Done` $\rightarrow$ `"Done"`
     - `State::Failed` $\rightarrow$ `"Failed"`
     - `State::Cancelled` $\rightarrow$ `"Cancelled"`
     - 그 외 잘못된 enum 값 $\rightarrow$ `"UnknownState"`

2. **`Event` (19개 고정 이벤트)**:
   - `UserInput`, `InferOk`, `ProviderError`, `BudgetExhausted`, `Cancel`,
     `NoAction`, `OneAction`, `MultipleActions`,
     `Deny`, `Ask`, `Allow`, `AuditError`,
     `Approved`, `RejectedOrExpired`,
     `ExecOk`, `ExecErrorOrIndeterminate`,
     `Continue`, `CompleteOrLimit`,
     `StartNextTurn`
   - `ToString(Event ev)` 반환 문자열:
     - `Event::UserInput` $\rightarrow$ `"UserInput"`
     - `Event::InferOk` $\rightarrow$ `"InferOk"`
     - `Event::ProviderError` $\rightarrow$ `"ProviderError"`
     - `Event::BudgetExhausted` $\rightarrow$ `"BudgetExhausted"`
     - `Event::Cancel` $\rightarrow$ `"Cancel"`
     - `Event::NoAction` $\rightarrow$ `"NoAction"`
     - `Event::OneAction` $\rightarrow$ `"OneAction"`
     - `Event::MultipleActions` $\rightarrow$ `"MultipleActions"`
     - `Event::Deny` $\rightarrow$ `"Deny"`
     - `Event::Ask` $\rightarrow$ `"Ask"`
     - `Event::Allow` $\rightarrow$ `"Allow"`
     - `Event::AuditError` $\rightarrow$ `"AuditError"`
     - `Event::Approved` $\rightarrow$ `"Approved"`
     - `Event::RejectedOrExpired` $\rightarrow$ `"RejectedOrExpired"`
     - `Event::ExecOk` $\rightarrow$ `"ExecOk"`
     - `Event::ExecErrorOrIndeterminate` $\rightarrow$ `"ExecErrorOrIndeterminate"`
     - `Event::Continue` $\rightarrow$ `"Continue"`
     - `Event::CompleteOrLimit` $\rightarrow$ `"CompleteOrLimit"`
     - `Event::StartNextTurn` $\rightarrow$ `"StartNextTurn"`
     - 그 외 잘못된 enum 값 $\rightarrow$ `"UnknownEvent"`

3. **`IsTerminal(State s)`**:
   - `s == State::Done || s == State::Failed || s == State::Cancelled` 인 경우에만 `true`, 그 외 `false`.

---

### 3-2. 명시 전이표 19개 (`kTransitions`)

헤더에 선언된 `constexpr std::array<Transition, 19> kTransitions`가 명시 전이의 유일한 단일 원천(SSOT)입니다:
```cpp
{State::Idle,          Event::UserInput,                State::Infer},
{State::Infer,         Event::InferOk,                  State::Propose},
{State::Infer,         Event::ProviderError,            State::Failed},
{State::Infer,         Event::BudgetExhausted,          State::Done},
{State::Propose,       Event::NoAction,                 State::Done},
{State::Propose,       Event::OneAction,                State::Gate},
{State::Propose,       Event::MultipleActions,          State::Failed},
{State::Gate,          Event::Deny,                     State::Observe},
{State::Gate,          Event::Ask,                      State::AwaitApproval},
{State::Gate,          Event::Allow,                    State::Execute},
{State::AwaitApproval, Event::Approved,                 State::Gate},
{State::AwaitApproval, Event::RejectedOrExpired,        State::Observe},
{State::Execute,       Event::ExecOk,                   State::Observe},
{State::Execute,       Event::ExecErrorOrIndeterminate, State::Observe},
{State::Observe,       Event::Continue,                 State::Infer},
{State::Observe,       Event::CompleteOrLimit,          State::Done},
{State::Done,          Event::StartNextTurn,            State::Idle},
{State::Failed,        Event::StartNextTurn,            State::Idle},
{State::Cancelled,     Event::StartNextTurn,            State::Idle}
```

---

### 3-3. 보편 규칙 R0~R4 해석기 (`Fsm::ResolveUniversal`)

`ResolveUniversal(State from, Event ev, State* to, bool* noop) noexcept`는 명시 전이표에 없는 보편 규칙을 결정론적으로 해석합니다:

1. **[R0] `Idle + Cancel`**:
   - `from == State::Idle && ev == Event::Cancel`
   - `*noop = true`, `*to = State::Idle`, 반환 `true`.
   - 전이 및 감사 기록 대상이 아니며, Dispatch는 `applied = false`, `Error::Ok()`를 반환합니다.

2. **[R4] 종료 상태 (`Done`, `Failed`, `Cancelled`) + `AuditError | Cancel`**:
   - `IsTerminal(from) && (ev == Event::AuditError || ev == Event::Cancel)`
   - `*noop = true`, `*to = from`, 반환 `true`.
   - 이미 종료된 턴은 재실패/재취소되지 않으며 no-op(`applied = false`, `Error::Ok()`) 처리합니다.

3. **[R1] `AuditError` $\rightarrow$ `Failed`**:
   - `ev == Event::AuditError`
   - 대상: `{Infer, Propose, Gate, AwaitApproval, Execute, Observe}` (비종료 활성 6개 상태)
   - `*noop = false`, `*to = State::Failed`, 반환 `true`.

4. **[R2] `Cancel` $\rightarrow$ `Cancelled`**:
   - `ev == Event::Cancel`
   - 대상: `{Infer, Propose, Gate, AwaitApproval, Observe}` (Execute 제외 비종료 5개 상태)
   - `*noop = false`, `*to = State::Cancelled`, 반환 `true`.

5. **[R3] `Execute` 상태의 `Cancel` 직접 수신 거부 및 `Timeout` 처리 규약**:
   - `Execute` 상태에서 `Dispatch(Event::Cancel)`가 호출되면 `ResolveUniversal`은 `false`를 반환합니다.
   - 명시 전이에도 없으므로 이는 **정의되지 않은 전이(Undefined Transition)**로 분류되어 fail-closed 원칙에 따라 상태가 즉시 `State::Failed`로 강제 전이되고 `Errc::Internal`이 반환됩니다.
   - **정상 취소/타임아웃 실행 규약**: 도구 실행 도중의 취소/타임아웃은 `CancelToken` 또는 타임아웃 타이머에 의해 Invoker 내부에서 처리되어 `ToolResultStatus::Cancelled`, `ToolResultStatus::Timeout`, 또는 `ToolResultStatus::Indeterminate`를 생성하고, FSM에는 `Event::ExecErrorOrIndeterminate` 이벤트로 전달되어 정상적으로 `Observe` 상태로 진입합니다.

6. **그 외 조합**:
   - `*noop = false`, 반환 `false`.

---

### 3-4. 단일 전이 진입점 (`Fsm::Dispatch`)

```cpp
Error Fsm::Dispatch(Event ev,
                    const std::string& cause,
                    const ActionId& action_id,
                    TurnId turn,
                    const Clock& clock,
                    TransitionRecord* out);
```

1. **전이 탐색 순서**:
   - 1단계: 명시 전이표 `kTransitions` 순회 (우선순위 1)
   - 2단계: `ResolveUniversal(s_, ev, &target, &noop)` 호출 (우선순위 2)
   - 3단계: 미발견 시 **정의되지 않은 전이(Undefined Transition)** 처리 (fail-closed)

2. **명시 전이 성공 시**:
   - `State prev = s_; s_ = target;`
   - `out`이 non-null인 경우:
     - `out->applied = true;`
     - `out->from = prev; out->to = target; out->ev = ev;`
     - `out->cause = cause; out->action_id = action_id; out->turn_id = turn;`
     - `out->wall_utc = clock.NowUtcRfc3339();`
     - `out->monotonic_ns = clock.MonotonicNs();`
     - `out->process_epoch_id = "";`
   - `Error::Ok()` 반환.

3. **보편 규칙 성공 시**:
   - `noop == true`인 경우 (R0, R4):
     - 상태 `s_` 변경 없음 (`s_` 유지).
     - `out`이 non-null인 경우: `out->applied = false;`, `out->from = s_; out->to = s_; out->ev = ev;`, 나머지 필드는 클록 및 인자값 기록.
     - `Error::Ok()` 반환.
   - `noop == false`인 경우 (R1, R2):
     - `State prev = s_; s_ = target;`
     - `out`이 non-null인 경우: `out->applied = true;`, `out->from = prev; out->to = target; out->ev = ev;`, 나머지 필드 기록.
     - `Error::Ok()` 반환.

4. **미정의 전이 (Undefined Transition) 실패 시**:
   - 상태 `s_`를 즉시 `State::Failed`로 강제 변경.
   - `out`이 non-null인 경우:
     - `out->applied = true;`
     - `out->from = prev; out->to = State::Failed; out->ev = ev;`
     - `out->cause = cause.empty() ? "undefined_transition" : cause;`
     - `out->action_id = action_id; out->turn_id = turn;`
     - `out->wall_utc = clock.NowUtcRfc3339();`
     - `out->monotonic_ns = clock.MonotonicNs();`
     - `out->process_epoch_id = "";`
   - `Errc::Internal` 반환.

---

### 3-5. 기동 전이표 무결성 검증 (`Fsm::VerifyTableIntegrity`)

기동 시 1회 호출되어 다음 4가지 조건을 검증하고, 위반 시 즉시 `Errc::Internal`을 반환합니다:

1. **중복 전이 검사**: `kTransitions` 내에 동일한 `(from, ev)` 쌍이 0건이어야 함.
2. **보편 규칙 충돌 검사**: `kTransitions` 내에 `Event::AuditError` 또는 `Event::Cancel`을 사용하는 명시 전이가 0건이어야 함.
3. **도달 가능성 검사**: `State::Idle`로부터 (명시 19개 + 보편 규칙 간선 기준) 모든 비종료 상태(`Infer`, `Propose`, `Gate`, `AwaitApproval`, `Execute`, `Observe`)에 도달 가능해야 함 (BFS/DFS 탐색).
4. **다음 턴 복귀 검사**: 모든 종료 상태(`Done`, `Failed`, `Cancelled`)에서 `Event::StartNextTurn`을 통해 `State::Idle`로 전이 가능해야 함.

---

### 3-6. 결정론적 전이표 직렬화 (`Fsm::DumpTable`)

대시보드 §12-9 및 CLI `--dump-transitions`를 위해 **총 37개의 유효 전이 조합**을 CCJ v1 정규 JSON Array로 직렬화하여 반환합니다:

1. **전이 객체 구조**:
   `{"event":"<EventStr>","from":"<FromStateStr>","kind":"explicit"|"universal","rule":"explicit"|"R0"|"R1"|"R2"|"R4","to":"<ToStateStr>"}`
2. **37개 항목의 결정론적 순서**:
   - 1~19번: `kTransitions` 순서대로 19개 명시 전이 (`"kind":"explicit"`, `"rule":"explicit"`)
   - 20번: R0 (Idle + Cancel $\rightarrow$ Idle, `"kind":"universal"`, `"rule":"R0"`)
   - 21~26번: R1 6개 상태 (`Infer`, `Propose`, `Gate`, `AwaitApproval`, `Execute`, `Observe`) + AuditError $\rightarrow$ Failed (`"kind":"universal"`, `"rule":"R1"`)
   - 27~31번: R2 5개 상태 (`Infer`, `Propose`, `Gate`, `AwaitApproval`, `Observe`) + Cancel $\rightarrow$ Cancelled (`"kind":"universal"`, `"rule":"R2"`)
   - 32~37번: R4 3개 종료 상태 (`Done`, `Failed`, `Cancelled`) × 2개 이벤트 (`AuditError`, `Cancel`) $\rightarrow$ 자가 상태 유지 (`"kind":"universal"`, `"rule":"R4"`)

---

## 4. Codex Write Scope (작업 허용 4개 파일)

Codex는 오직 아래 4개 파일만 작성/수정합니다:

```text
[빌드 스크립트]
- src/CMakeLists.txt   (fsm.cpp 추가)
- tests/CMakeLists.txt (fsm_test.cpp 추가)

[구현 소스]
- src/fsm.cpp

[단위 테스트]
- tests/fsm_test.cpp
```

---

## 5. Verification Deliverables (검증 요구사항)

1. **빌드 무결성**:
   - GCC 및 Clang 환경에서 경고 없이 빌드 완료 (`-Wall -Wextra -Wpedantic -Werror`).
2. **단위 테스트 전수 통과 (`tests/fsm_test.cpp`)**:
   - `core/fsm.string_mapping`: 10개 State, 19개 Event의 `ToString` 정확성 및 유효하지 않은 enum의 `"UnknownState"`, `"UnknownEvent"` 반환 검증.
   - `core/fsm.terminal_classification`: `IsTerminal` 3개 상태 정확성 검증.
   - `core/fsm.explicit_transitions`: 19개 명시 전이 정상 전이 및 `TransitionRecord` 필드 검증.
   - `core/fsm.universal_r0_idle_cancel`: Idle + Cancel $\rightarrow$ 상태 불변, `applied == false`, `Error::Ok()`.
   - `core/fsm.universal_r1_audit_error`: 활성 6개 상태에서 `State::Failed` 전이, `applied == true`.
   - `core/fsm.universal_r2_cancel`: 활성 5개 상태에서 `State::Cancelled` 전이, `applied == true`.
   - `core/fsm.universal_r3_execute_cancel_reject`: Execute + Cancel $\rightarrow$ undefined transition 분류, 상태 `Failed` 강제, `Errc::Internal` 반환.
   - `core/fsm.universal_r4_terminal_noop`: 종료 3개 상태 + AuditError/Cancel $\rightarrow$ 상태 불변, `applied == false`, `Error::Ok()`.
   - `core/fsm.undefined_transitions`: 임의의 미정의 (State, Event) 조합 시 즉시 `Failed` 강제 및 `Errc::Internal` 반환.
   - `core/fsm.table_integrity`: `VerifyTableIntegrity()` 성공 검증.
   - `core/fsm.dump_table_golden`: `DumpTable()` 호출 결과가 37개 요소를 정확한 순서와 속성으로 포함하는지 CCJ 파싱 및 직렬화 검증.
   - `core/fsm.consecutive_turns`: `StartNextTurn`을 통한 다중 턴 라이프사이클 정상 전이 검증.
3. **정적 분석 및 메모리 무결성**:
   - ASan / UBSan 실행 시 메모리 누수 및 UB 0건.
4. **`git diff --check`**: 공백 오류 0건.
