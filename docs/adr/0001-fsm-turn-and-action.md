# ADR-0001: 턴·Action 실행 경로의 결정론적 게이트웨이

- **상태**: Proposed
- **날짜**: 2026-08-21
- **결정권자**: 아키텍트, 안전 책임자
- **관련 G0**: G0-24(Cancel/R1 집합), G0-31(재진입 카운터)
- **관련 명세**: `Cogito++_구현명세서.md` §4-10, §5, §6-2-a, §6-3, §6-4, §12-3
- **근거 보고서**: `docs/g0/G0-RESOLUTION-9.md` ⑤ ⑨
- **되돌림 가능성**: **가능.** 감사 payload 형식을 바꾸지 않는다. 다만 골든 리플레이 픽스처는 재생성해야 한다

---

## 맥락

FSM 은 "LLM 의 판단이 실제 행동으로 이어지는 경로를 코드 수준에서 결정론적으로 만든다"는
프로젝트 핵심 주장의 구현체다. 그런데 규범이 두 곳에 다르게 적혀 있었다.

| 위치 | 서술 |
| --- | --- |
| §4-10 주석 | `R1 AuditError : {Infer, Propose, Gate, AwaitApproval, Execute, Observe}` — Idle 제외 |
| §5 `ResolveUniversal` 코드 | `if (IsTerminal(from)) return false;` 이후 무조건 적용 — **Idle 포함** |
| §4-10 주석 | `R2 Cancel : {Idle, …}` — Idle **포함** |

`Idle` 에서 `Cancelled`/`Failed` 로 전이하면 **`turn_begin` 없는 턴에 `turn_end` 가 생긴다.**
불변식 12("모든 턴은 turn_end 정확히 1개")와 감사 체인의 턴 경계 가정이 동시에 깨진다.

또한 `gate_reentry_count` 는 Gate 7단계에서 **읽히기만 하고 증가·초기화 시점이 없었다.**
구현자가 초기 `Ask` 에서 증가시키면 첫 승인 후 재진입이 즉시 상한에 걸려 정상 승인 흐름이 끊긴다.

---

## 결정

### D1. 보편 규칙 R0~R4 (명시 전이표 19개에 더해 이것이 전부다)

```
[R0] Idle + Cancel                -> no-op. 전이·감사 없음. Dispatch 는 Ok 반환.
[R1] AuditError                   -> Failed
     대상: {Infer, Propose, Gate, AwaitApproval, Execute, Observe}
     Idle 과 종료 상태는 대상이 아니다.
[R2] Cancel                       -> Cancelled
     대상: {Infer, Propose, Gate, AwaitApproval, Observe}
     Idle(R0) · Execute(R3) · 종료 상태는 대상이 아니다.
[R3] Execute 는 Cancel 을 이벤트로 받지 않는다.
     CancelToken 이 Invoker 로 전달되고 결과가 ToolResult 로 분류된다.
       effect == None -> Cancelled          (깨끗한 취소)
       effect != None -> Indeterminate      (불변식 9 — 설비 상태를 알 수 없다)
     둘 다 Event::ExecErrorOrIndeterminate 로 Observe 진입.
     Observe 에서 CancelToken 이 여전히 set 이면 R2 로 Cancelled.
[R4] 종료 상태(Done/Failed/Cancelled) + AuditError|Cancel -> no-op. Ok 반환.
```

**R3 가 이 ADR 의 안전 핵심이다.** 쓰기 중 취소를 "깨끗한 취소"로 기록하면
설비가 바뀌었는지 모르는 상태를 성공/실패로 뭉개게 되고 불변식 9 가 무력화된다.

### D2. `turn_begin` 은 전이보다 먼저 커밋한다

`Idle → Infer` 전이 **이전에** `turn_begin` 을 커밋하고, 실패하면
**FSM 을 전혀 움직이지 않고** 오류를 반환한다. 그래서 `Idle` 에서는 `AuditError` 가 발생할 수 없다(R1 이 Idle 을 제외하는 근거).

### D3. commit-before-transition (§6-2-a 재확인)

```
verdict 산출(Gate 1~7)
  -> [모든 판정 공통] audit.Commit(verdict)        ← 상태 전이 이전
  -> Deny  : Fire(Deny)  -> Observe
  -> Ask   : CreatePending -> Commit(approval_requested) -> Fire(Ask) -> AwaitApproval
  -> Allow : Commit(tool_call_started + idempotency_key) ← Permit 발급 이전
             -> Consume(approval) -> IssuePermit -> Fire(Allow) -> Execute
```

어떤 판정도 이 순서를 벗어나지 않는다. 감사 커밋 실패는 항상 R1 로 `Failed` 다.

### D4. 재진입 카운터

| 항목 | 확정 |
| --- | --- |
| 증가 시점 | `AwaitApproval → Gate` (`Event::Approved`) Dispatch 시, owner thread 가 증가 |
| 초기 `Ask` | **증가하지 않는다.** 최초 진입은 재진입이 아니다 |
| 상한 | **1** |
| 초기화 | Action 이 `Observe` 도달 시 해당 `action_id` erase / `turn_end` 성공 시 전체 clear |
| 원자성 | owner thread 전용이므로 atomic 불필요. 증가와 그 전이의 감사는 **같은 스텝**에서 |

`FindUsable` 이 nullptr 일 때 이유를 구분해 보고한다(뭉개면 사후 조사가 불가능하다).

```
① 만료          -> approval_expired
② 이미 소비     -> approval_already_consumed
③ scope 불일치  -> approval_scope_mismatch
④ 없음+count>=1 -> approval_reentry_exceeded
⑤ 없음+count==0 -> Ask (정상)
```

### D5. `ResetForNextTurn()` 직접 대입 제거

`Done/Failed/Cancelled → Idle` 은 `Event::StartNextTurn` Dispatch 로만 일어나며,
다른 전이와 동일하게 `TransitionRecord` 를 만들고 감사된다.
직접 상태 대입은 테스트 픽스처 전용(`ResetForTestOnly`)으로 격리한다.

### D6. `DumpTable()` 은 R0~R4 로 생성되는 전이도 내보낸다

§12-9 가 대시보드 FSM 그래프를 `cogito_dump_transitions` 출력에서만 생성하도록 규정했다.
보편 규칙 전이가 빠지면 **화면이 실제 실행 경로와 달라진다.**

---

## 대안과 기각 이유

| # | 대안 | 장점 | 기각 이유 |
| --- | --- | --- | --- |
| 1 | R1/R2 를 모든 비종료 상태에 적용(코드 현행) | 코드가 단순 | `Idle` 에서 `turn_begin` 없는 턴이 생겨 불변식 12 위반 |
| 2 | `Execute` 도 `Cancel` 이벤트를 받게 함 | 전이표가 균일 | 쓰기 중 취소가 `Cancelled` 로 기록되어 불변식 9 무력화. **안전상 채택 불가** |
| 3 | 재진입 카운터를 초기 `Ask` 에서 증가 | 구현이 단순 | 첫 승인 후 재진입이 즉시 상한에 걸려 정상 흐름이 끊긴다 |
| 4 | 재진입 상한을 2 이상 | 정책 변경 시 유연 | 승인 피로. 같은 화면을 반복해 보면 운영자가 내용을 읽지 않고 누른다 |
| 5 | 카운터를 turn_end 에만 clear | 코드가 단순 | 장기 세션에서 map 무한 증가 |

---

## 안전 영향

| 불변식 | 영향 |
| --- | --- |
| **9** (재시도 금지·indeterminate) | R3 가 직접 구현한다. 대안 2 를 택하면 무력화된다 |
| **12** (turn_end 정확히 1회) | R0/R4 와 D2 가 `turn_begin` 없는 턴을 구조적으로 막는다 |
| **5·6** (승인 결합·재검증) | D4 의 판정 순서가 만료·소비·scope 불일치를 구분해 오분류를 막는다 |
| **7·8** (감사 선행·실패 시 차단) | D3 가 순서를 고정한다 |
| **11** (단일 스레드 소유) | D4 의 카운터가 owner thread 전용임을 명시 |

---

## 롤백·마이그레이션 영향

- 감사 payload **형식은 바뀌지 않는다.** 기존 감사 데이터는 유효하다
- 골든 리플레이 픽스처는 **재생성 필요** — 전이 시퀀스가 달라진다(`StartNextTurn` 이 감사에 남는다)
- `DumpTable()` 출력이 커지므로 §10-1 골든 키의 전이표 digest 가 바뀐다
- **Codex 인계**: `src/fsm.cpp` `ResolveUniversal`, `src/agent_loop.cpp` 재진입 카운터
- **Antigravity 인계**: FSM 그래프는 `/api/transitions` 응답만 쓴다. 상태 이름 하드코딩 금지

---

## 검증 방법

| 테스트 | 확인 |
| --- | --- |
| `core/fsm.transition_table_integrity` | (from,event) 중복 0, 명시표에 AuditError/Cancel 없음, Idle 로부터 모든 비종료 상태 도달 |
| `core/fsm.idle_cancel_noop` | Idle+Cancel → 상태 불변, 감사 이벤트 0건, Ok 반환 |
| `core/fsm.terminal_noop` | Done/Failed/Cancelled + AuditError|Cancel → 상태 불변 |
| `core/fsm.execute_rejects_cancel` | Execute+Cancel → `ResolveUniversal` 이 false, 미정의 전이 처리로 가지 않음 |
| `core/fsm.cancel_during_write_is_indeterminate` | effect!=none 실행 중 취소 → `Indeterminate`, `Cancelled` 아님 |
| `core/fsm.consecutive_turns` | 정상→정상, 실패→정상, 취소→정상 2턴 이상 |
| `core/approval.reentry_sequence` | Ask→승인→Allow (count 1), 정책 변경 후 2회째 Ask → `approval_reentry_exceeded` |
| `core/approval.reentry_reset` | Observe 도달 시 erase, 새 action 에 미전파 |
| `core/gate.findusable_reason_codes` | 만료/소비/scope 불일치/재진입초과 5가지가 각각 다른 reason_code |
| `audit/ordering.commit_before_transition` | 감사 커밋 실패 주입 시 handler 호출 0회 |

---

## 미결

- **`kVerdictTtlNs` 의 구체값** — Verdict 유효기간과 승인 대기 시간(`approval_timeout_ms=120000`)의 관계.
  Verdict TTL 이 승인 대기보다 짧으면 승인해도 항상 만료된다. 아키텍트 결정 필요
- **`Observe` 에서의 `AuditError` 처리 순서** — `tool_result` 커밋 실패 시 R1 로 `Failed` 가 되는데,
  그 시점에 이미 설비 write 는 일어났다. `Failed` 로 끝난 턴에 성공한 write 가 남는 것을
  감사에서 어떻게 표현할지 별도 규범 필요 (ADR-0004 와 연계)
