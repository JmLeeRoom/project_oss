// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 턴 실행 상태기계
//
// 규범 근거 : Cogito++_구현명세서.md §4-10, §5
// G0 결정   : G0-24 (docs/g0/G0-RESOLUTION-9.md ⑤), ADR-0001 D1·D5·D6
//
// 이 파일이 "LLM 의 판단이 행동으로 이어지는 경로"의 유일한 규범이다.
// 전이는 명시표 19개 + 보편 규칙 R0~R4 가 전부다. 다른 곳에서 상태를 바꾸지 않는다.
//
// ⚠ ADR-0001 (D1, D5, D6) 및 G0-RESOLUTION-9 승인 완료. 본 헤더가 정식 규범으로 확정됨.
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
enum class ToolResultStatus : std::uint8_t;

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
 *  [R3] Execute + Cancel -> no-op. (ADR-0001 확정)
 *       Execute 는 Cancel 을 이벤트로 처리하지 않고 무시(no-op)하여 미정의 전이로 빠지지 않게 한다.
 *       실행 중 취소는 CancelToken 으로 Invoker 에 전달되고 ToolResult 로 분류된다.
 *         effect == None  ->  ToolResultStatus::Cancelled
 *         effect != None  ->  ToolResultStatus::Indeterminate     (불변식 9)
 *       둘 다 Event::ExecErrorOrIndeterminate 로 Observe 에 진입한다.
 *       Observe 에서 CancelToken 이 여전히 set 이면 R2 로 Cancelled 로 간다.
 *       ※ 쓰기 중 취소를 '깨끗한 취소'로 기록하면 불변식 9 가 무력화된다.
 *
 *       [Gemini 확정] `ToolResultStatus::Timeout`은 상태와 무관하게 `Event::ExecErrorOrIndeterminate`로 매핑하여 Observe로 진입한다. (ADR-0001 보완)
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

  // Timeout 및 실행 결과를 FSM 이벤트로 변환하는 맵핑 규약 (ADR-0001 보완)
  static Event MapToolResultStatus(ToolResultStatus status) noexcept;

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
