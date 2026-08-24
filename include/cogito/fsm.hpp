// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 턴 실행 상태기계
//
// 규범 근거 : Cogito++_구현명세서.md §4-10, §5
// G0 결정   : G0-24 (docs/g0/G0-RESOLUTION-9.md ⑤), ADR-0001 D1·D5·D6
//
// 이 파일이 "LLM 의 판단이 행동으로 이어지는 경로"의 유일한 규범이다.
// 전이는 명시표 19개 + 보편 규칙 R0~R4 가 전부다. 다른 곳에서 상태를 바꾸지 않는다.
//
// ⚠ 선반영 고지 — 이 파일은 G0-RESOLUTION-9 / ADR-0001 의 **Proposed** 결정을 선반영한
//    초안이며 승인 전에는 규범이 아니다. 승인 전까지 이 헤더를 구현 기준으로 인계하지 않는다.
//    승인 시 이 고지를 제거한다. (승인 상태: docs/g0/G0-LEDGER.md)
//
// ⚠⚠ 이 파일은 현행 규범 명세와 **실제로 충돌한다.** 다른 헤더보다 위험도가 높다.
//     ADR-0001 이 Proposed 이므로 현재 권위는 `Cogito++_구현명세서.md` 다. 그런데:
//
//       이 헤더                          | 구현명세서 (현행 규범)
//       ---------------------------------|------------------------------------------
//       R0~R4 (5개 규칙)                 | R1/R2/R3 (3개). `grep R0/R4` -> 0 hit
//       R2 대상에서 Idle 제외            | :972-974 의 R2 에 Idle 이 **포함**
//       ResetForTestOnly (테스트 전용)   | :1003 ResetForNextTurn() 직접 대입
//
//     즉 **헤더를 보고 구현하면 명세 위반, 명세를 보고 구현하면 헤더 위반**이다.
//     어느 쪽도 통과할 수 없다. Codex 에게 이 헤더를 구현 기준으로 넘기기 전에
//     ADR-0001 승인(→ 헤더가 규범이 됨) 또는 명세 §4-10·§5 정정이 선행해야 한다.
#ifndef COGITO_FSM_HPP
#define COGITO_FSM_HPP

#include <array>
#include <cstdint>
#include <string>

#include "cogito/canonical_json.hpp"
#include "cogito/ids.hpp"
#include "cogito/result.hpp"

namespace cogito {

class Clock;

// ─────────────────────────────────────────────────────────────────────────────
enum class State : std::uint8_t {
  Idle, Infer, Propose, Gate, AwaitApproval, Execute, Observe,
  Done, Failed, Cancelled
};

enum class Event : std::uint8_t {
  UserInput, InferOk, ProviderError, BudgetExhausted, Cancel,
  NoAction, OneAction, MultipleActions,
  Deny, Ask, Allow, AuditError,
  Approved, RejectedOrExpired,
  ExecOk, ExecErrorOrIndeterminate,
  Continue, CompleteOrLimit,
  StartNextTurn
};

// 안정 문자열. 감사·DumpTable·대시보드가 이 값을 쓴다(§12-9).
// 이 문자열을 바꾸면 골든 리플레이와 FSM 그래프가 함께 깨진다.
const char* ToString(State) noexcept;
const char* ToString(Event) noexcept;

bool IsTerminal(State) noexcept;   // Done | Failed | Cancelled

// ─────────────────────────────────────────────────────────────────────────────
struct Transition { State from; Event ev; State to; };

// 명시 전이표 — 단일 source of truth. 문서용 사본을 따로 두지 않는다.
inline constexpr std::array<Transition, 19> kTransitions{{
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
  {State::Cancelled,     Event::StartNextTurn,            State::Idle},
}};

/*  보편 규칙 — 명시표에 없지만 유효한 전이 (ADR-0001 D1)
 *
 *  [R0] Idle + Cancel  ->  no-op. 전이도 감사도 없고 Dispatch 는 Ok 를 반환한다.
 *       진행 중인 턴이 없으므로 취소할 대상이 없다.
 *
 *  [R1] AuditError  ->  Failed
 *       대상: {Infer, Propose, Gate, AwaitApproval, Execute, Observe}
 *       Idle 은 대상이 아니다 — turn_begin 커밋은 Idle->Infer 전이 '이전에' 수행되고
 *       실패하면 FSM 을 움직이지 않으므로, Idle 에서 AuditError 가 발생할 수 없다.
 *       (ADR-0001 D2. 이 규칙이 없으면 turn_begin 없는 턴에 turn_end 가 생긴다)
 *
 *  [R2] Cancel  ->  Cancelled
 *       대상: {Infer, Propose, Gate, AwaitApproval, Observe}
 *       Idle(R0) · Execute(R3) · 종료 상태(R4)는 대상이 아니다.
 *
 *  [R3] Execute 는 Cancel 을 이벤트로 받지 않는다.
 *       실행 중 취소는 CancelToken 으로 Invoker 에 전달되고 ToolResult 로 분류된다.
 *         effect == None  ->  ToolResultStatus::Cancelled
 *         effect != None  ->  ToolResultStatus::Indeterminate     (불변식 9)
 *       둘 다 Event::ExecErrorOrIndeterminate 로 Observe 에 진입한다.
 *       Observe 에서 CancelToken 이 여전히 set 이면 R2 로 Cancelled 로 간다.
 *       ※ 쓰기 중 취소를 '깨끗한 취소'로 기록하면 불변식 9 가 무력화된다.
 *
 *       ⚠ 미결 — `ToolResultStatus::Timeout` 의 FSM 사상이 규범 어디에도 없다.
 *         실측: 명세 :540 이 `Timeout` 을 정의하지만, §4-10 명시 전이표(:963-964)에서
 *         Execute 를 떠나는 이벤트는 `ExecOk` 와 `ExecErrorOrIndeterminate` 둘뿐이고,
 *         :979 의 보편 규칙 서술은 취소 두 경우만 다룬다. ADR-0001 에는 `Timeout` 이
 *         한 번도 등장하지 않는다(grep 0 hit).
 *         `ExecOk` 로 메우면 **타임아웃이 성공으로 기록된다** — 그래서 빈칸으로 두면 안 된다.
 *         Codex 가 임의로 메우지 말고 §4-10·§5 정정 또는 ADR-0001 보완으로 확정할 것.
 *         (effect != None 인 타임아웃은 애초에 Timeout 이 아니라 Indeterminate 다 —
 *          invoker.hpp ToolInvoker::Invoke [실행 계약] 7. 따라서 미결 범위는
 *          effect == None 인 Timeout 하나다.)
 *
 *  [R4] 종료 상태(Done/Failed/Cancelled) + AuditError|Cancel  ->  no-op. Ok 반환.
 *       이미 끝난 턴을 다시 실패시키지 않는다.
 */

// ─────────────────────────────────────────────────────────────────────────────
struct TransitionRecord {
  // false 이면 R0/R4 no-op 이다. 감사에 기록하지 않는다.
  bool         applied = false;

  State        from = State::Idle;
  State        to   = State::Idle;
  Event        ev   = Event::UserInput;

  std::string  cause;          // reason_code 또는 자유 문자열
  ActionId     action_id;      // 해당 없으면 빈 문자열
  TurnId       turn_id = 0;

  std::string  wall_utc;       // 표시·상관관계 전용
  std::int64_t monotonic_ns = 0;
  std::string  process_epoch_id;
};

// ─────────────────────────────────────────────────────────────────────────────
class Fsm {
 public:
  State current() const noexcept { return s_; }

  // 전이의 유일한 진입점.
  //
  // 반환:
  //   Ok               — 전이 성공(out->applied == true) 또는 R0/R4 no-op(applied == false)
  //   Errc::Internal   — 정의되지 않은 전이. 상태를 즉시 Failed 로 바꾸고
  //                      out 에 undefined_transition 기록을 채운다(§7-3).
  //
  // @thread: agent-loop-only
  [[nodiscard]] Error Dispatch(Event ev,
                               const std::string& cause,
                               const ActionId& action_id,
                               TurnId turn,
                               const Clock& clock,
                               TransitionRecord* out);

  // --dump-transitions.
  // 명시 19개 + R0~R4 로 생성되는 전이를 '모두' 내보낸다(ADR-0001 D6).
  // §12-9 가 대시보드 FSM 그래프를 이 출력에서만 생성하도록 규정했으므로,
  // 보편 규칙 전이가 빠지면 화면이 실제 실행 경로와 달라진다.
  // 출력은 CCJ v1 으로 직렬화되며 배열 순서가 고정이다.
  static ccj::Json DumpTable();

  // 기동 시 1회. 실패하면 프로세스 시작을 실패시킨다.
  //   - (from, event) 중복 없음
  //   - 명시표에 AuditError/Cancel 이 없음(보편 규칙과 충돌 금지)
  //   - Idle 로부터 모든 비종료 상태 도달 가능 (명시 + 보편 규칙 간선 기준)
  //   - 모든 종료 상태에서 StartNextTurn 으로 Idle 도달 가능
  [[nodiscard]] static Error VerifyTableIntegrity();

  // 테스트 픽스처 전용. 감사를 남기지 않으므로 제품 경로에서 호출을 금지한다.
  // 정상 경로의 다음 턴 진입은 Dispatch(StartNextTurn) 로만 한다(ADR-0001 D5).
  void ResetForTestOnly(State s) noexcept { s_ = s; }

 private:
  // R0~R4 를 해석한다.
  //   반환 true  : *to 에 목적 상태를 채웠다(applied)
  //   반환 false : 이 규칙으로 처리되지 않았다
  //   *noop=true : R0/R4 에 해당한다. 전이하지 않고 Ok 로 끝낸다
  static bool ResolveUniversal(State from, Event ev, State* to, bool* noop) noexcept;

  State s_ = State::Idle;
};

}  // namespace cogito

#endif  // COGITO_FSM_HPP
