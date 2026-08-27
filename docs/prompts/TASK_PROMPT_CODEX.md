# [Gemini Task] Codex TICKET-S6-CORE-ENGINE-001 Conversation, Invoker, AgentLoop 통합 구현 (체크리스트 S6-01 ~ S6-12)

> **작성자**: Gemini (설계·계약 총괄)
> **수행자**: Codex (구현·빌드·테스트 전담)
> **기준일**: 2026-08-26
> **티켓**: **TICKET-S6-CORE-ENGINE-001 (체크리스트 S6-01 ~ S6-12)**
> **승인 상태**: G0-02, G0-04, G0-05, G0-06, G0-07, G0-25, G0-27, G0-28, G0-31 및 ADR-0001, ADR-0004 승인 완료.

---

## 1. Objective (단일 집중 티켓 목표)

S1~S5에서 구축된 기초 모듈(CCJ, FSM, Registry, Policy, Budget, Permit, Gate, Audit)을 종합 연동하는 **Cogito++ 핵심 에이전트 실행 엔진(Conversation, ToolInvoker, Inference, AgentLoop)**을 구현하고 전체 통합 시나리오 테스트를 전수 통과시킵니다.

---

## 2. Header & Contract Specification (Gemini 소유 공개 헤더 참조)

Codex는 아래 선언된 공개 헤더 계약을 100% 준수합니다:

### 2-1. 신규 및 기존 헤더 계약 목록
- `include/cogito/conversation.hpp`: `Role`, `Message`, `ConversationStore`
- `include/cogito/context_compactor.hpp`: `ContextCompactor`, `CompactionResult`, `MakeDropOldestObservationCompactor()`, `MakeNoopCompactor()`
- `include/cogito/inference.hpp`: `Usage`, `FinishReason`, `CancelToken`, `ProviderIdentity`, `InferenceRequest`, `InferenceResponse`, `InferenceAdapter`, `FakeProvider`
- `include/cogito/invoker.hpp`: `ToolCallContext`, `ToolInvoker`, `MakeInvalidToolResult`
- `include/cogito/agent_loop.hpp`: `TurnStatus`, `TurnOutcome`, `AgentDeps`, `AgentLoopConfig`, `AgentLoop`
- `include/cogito/permission_gate.hpp`: `GateInput`, `ApprovalRecord`, `ApprovalStore`, `ApprovalLookupResult`, `PermissionGate`
- `include/cogito/audit.hpp`: `AuditEvent`, `AuditJournal`, `RecordingAuditJournal`
- `include/cogito/fsm.hpp`: `Fsm`, `State`, `Event`, `TransitionRecord`
- `include/cogito/permit.hpp`: `ExecutionPermit`
- `include/cogito/budget.hpp`: `BudgetTracker`, `TurnBudget`

---

## 3. Implementation Specification (세부 구현 규범)

### 3-1. ConversationStore & ContextCompactor (S6-01)
1. `src/conversation.cpp`:
   - `MakeDropOldestObservationCompactor()`:
     - 컨텍스트 크기가 `context_soft_limit_bytes`를 초과할 경우 가장 오래된 `Role::Tool` 메시지부터 제거.
     - **보존 필수 3요소**: (1) `Role::System` 정책 메시지, (2) 미결 Action 관련 메시지, (3) 가장 최근 `Role::Tool` 결과.
     - `untrusted` 속성 및 출처(`provenance`) 보존.
     - 버전 문자열: `"drop-oldest-observation-v1.0"`.
   - `MakeNoopCompactor()`:
     - 축약 없이 `compacted = false` 반환. 버전 문자열: `"none-v1.0"`.

### 3-2. ToolInvoker (S6-03, S6-04)
1. `src/invoker.cpp`:
   - `ToolInvoker::Invoke(ExecutionPermit&& permit, const ccj::Json& arguments, const ToolCallContext& ctx)`:
     - **Permit 유효성 검사**: `permit.CheckUsable(td.name, action_digest, scope_digest, now_ns)`.
     - **Permit 단일 소비**: 핸들러 실행 직전에 `permit.Consume()`.
     - **핸들러 실행**: 최대 1회 (재시도 0회).
     - **결과 검증**: 크기 상한(`max_output_bytes`) -> Strict JSON 파싱 -> `output_schema` 검증 (G0-27). 위반 시 `MakeInvalidToolResult` 주입.
     - **예외 격리**: 핸들러의 예외를 포착하여 `ToolResultStatus::Error`로 변환 (C ABI 전파 차단).
     - **취소 및 타임아웃 분류 (ADR-0001 R3)**:
       - `effect == Effect::None`: 취소 시 `Cancelled`, 타임아웃 시 `Timeout`.
       - `effect != Effect::None`: 취소 및 타임아웃 시 모두 **`Indeterminate`** 로 분류 (설비 상태 불확실성).

### 3-3. AgentLoop 핵심 오케스트레이션 (S6-05 ~ S6-12)
1. `src/agent_loop.cpp`:
   - **단일 스레드 가드 (`in_call_`)**: 동시 호출 또는 재진입 시 `Errc::ConcurrentAccess` 또는 즉시 오류 반환.
   - **`RunTurn(user_input)` & `ResumeTurn()` 수렴**:
     - `turn_begin` 감사 커밋 (`AuditJournal::Commit`) -> FSM 상태 전이.
     - 대화 기록에 사용자 입력 추가 -> 토큰 예약(`BudgetTracker::ReserveTokens`).
     - `InferenceAdapter::Complete` 호출.
     - Action 미요청 시 -> `Finalize(Completed)`.
     - Action 제안 시 -> `PermissionGate::Evaluate` 실행.
   - **Gate 커밋 프로토콜 (§6-2-a)**:
     - **Deny 판정**: `verdict` 감사 커밋 -> `Fire(Event::Deny)` -> 거부 메시지 대화 주입 -> `Finalize(Completed 또는 Failed)`.
     - **Ask 판정**: `approval_requested` 감사 커밋 -> `Fire(Event::Ask)` -> `pending_action_` 보관 -> `TurnOutcome{Status::PendingApproval}` 반환 (턴 종료 커밋 안 함).
     - **Allow 판정**:
       - `tool_call_started` (with `idempotency_key`) 감사 커밋 -> `gate_.IssuePermit` -> `Fire(Event::Allow)` -> `ToolInvoker::Invoke` -> `tool_result` 감사 커밋 -> `Fire(Event::Observe)`.
       - 결과 대화 주입 및 토큰 정산 -> 다음 추론 또는 턴 마무리.
   - **불확실성 잠금 (`IndeterminateLockdown`)**:
     - `ToolResultStatus::Indeterminate` 발생 시 `indeterminate_locks_.insert(operation_digest)`.
     - 이후 동일 `operation_digest` 요청은 Gate 5단계에서 거부.
     - `AcknowledgeIndeterminate`: `operator_ack` 감사 성공 시에만 해당 잠금 해제.
   - **세션 봉인 및 종료 (`SealSession`, `RetryFinalize`)**:
     - `turn_end` 감사 커밋 실패 시 `finalize_pending_ = true` 설정 및 다음 턴 진입 차단.
     - `RetryFinalize()`로 동일 payload 멱등 재커밋 시도.

---

## 4. Codex Write Scope (작업 허용 파일)

```text
[빌드 스크립트]
- src/CMakeLists.txt
- tests/CMakeLists.txt

[구현 소스]
- src/conversation.cpp
- src/invoker.cpp
- src/agent_loop.cpp

[단위 테스트]
- tests/conversation_test.cpp
- tests/invoker_test.cpp
- tests/loop_test.cpp
```

---

## 5. Verification Deliverables (검증 요구사항)

1. **컴파일 및 빌드**: MSVC / GCC / Clang 경고 0건 (`/W4 /WX`).
2. **신규 단위 테스트 전수 통과**:
   - `conversation_test`: 메시지 보존, 컨텍스트 축약 시 보호 3요소 보존, untrusted 플래그 유지 검증.
   - `invoker_test`: Permit 소비 순서, 출력 스키마 위반 격리, 예외 방어, 부수효과 도구의 Cancelled/Timeout -> Indeterminate 분류 검증.
   - `loop_test`: RunTurn 정상 대화, Tool 실행(Allow), 승인 대기(Ask) -> ResumeTurn(Approved), 거부(Deny), Indeterminate 발생 및 Acknowledge 복구, CancelToken 취소, 감사 실패 시 롤백 검증.
3. **전체 회귀 테스트 100% 통과**: S1 ~ S5 포함 전체 테스트 전수 통과.
4. **정적 분석**: `git diff --check` 공백 오류 0건.