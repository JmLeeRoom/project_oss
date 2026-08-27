// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 에이전트 핵심 루프 (AgentLoop & Turn Lifecycle)
//
// 규범 근거 : Cogito++_구현명세서.md §4-12(:1220-1282), §6-2-a(:1617-1645), §3 불변식 1~12
// G0 결정   : G0-02 (요청별 주체), G0-04 (Indeterminate 잠금), G0-06 (pending_turn_end 멱등 재커밋),
//             G0-07 (원자적 취소), G0-28 (타임아웃/취소 격리), G0-31 (재진입 상한)
//
// [핵심 루프 불변식]
// 1. 단일 소유자 스레드: 동일 인스턴스에서 동시 진입 금지 (in_call_ 가드로 검출).
// 2. Gate 커밋 프로토콜: Audit Commit -> FSM Transition -> Tool Execution 순서 엄수.
// 3. 쓰기 작업 1회성: 쓰기/파괴적 도구는 재시도 없이 최대 1회만 실행 (불변식 9).
// 4. 불확실성 잠금: 크래시 복구 또는 실행 중단된 작업은 operator_ack 이전까지 잠금 유지.
#ifndef COGITO_AGENT_LOOP_HPP
#define COGITO_AGENT_LOOP_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cogito/action.hpp"
#include "cogito/audit.hpp"
#include "cogito/budget.hpp"
#include "cogito/canonical_json.hpp"
#include "cogito/clock.hpp"
#include "cogito/config.hpp"
#include "cogito/context_compactor.hpp"
#include "cogito/conversation.hpp"
#include "cogito/fsm.hpp"
#include "cogito/identity.hpp"
#include "cogito/ids.hpp"
#include "cogito/inference.hpp"
#include "cogito/invoker.hpp"
#include "cogito/permission_gate.hpp"
#include "cogito/policy.hpp"
#include "cogito/registry.hpp"
#include "cogito/result.hpp"

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// TurnStatus — 턴 종료 상태 분류
// ─────────────────────────────────────────────────────────────────────────────
enum class TurnStatus : std::uint8_t {
  Completed       = 1,   // 정상 완료 (텍스트 응답 또는 허가된 작업 완료)
  PendingApproval = 2,   // 작업자 승인 대기 중 (Turn 일시 정지)
  Cancelled       = 3,   // 사용자 또는 운영자에 의해 취소됨
  Failed          = 4,   // 오류 또는 불변식 위반으로 실패
};

inline constexpr std::string_view ToString(TurnStatus status) noexcept {
  switch (status) {
    case TurnStatus::Completed:       return "completed";
    case TurnStatus::PendingApproval: return "pending_approval";
    case TurnStatus::Cancelled:       return "cancelled";
    case TurnStatus::Failed:          return "failed";
  }
  return "unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// TurnOutcome — 단일 턴 실행 결과 요약
// ─────────────────────────────────────────────────────────────────────────────
struct TurnOutcome {
  TurnStatus                    status = TurnStatus::Failed;
  std::string                   text;                      // 어시스턴트 최종 응답 텍스트
  State                         final_state = State::Failed; // 최종 FSM 상태
  std::string                   pending_action_id;         // status==PendingApproval 시 대상 액션 ID
  std::string                   pending_approval_id;       // status==PendingApproval 시 승인 ID
  bool                          had_indeterminate = false; // 불확실(Indeterminate) 실행 발생 여부
  Usage                         usage;                     // 누적 토큰 소비량
  std::vector<TransitionRecord> transitions;               // FSM 전이 기록
  std::vector<Verdict>          verdicts;                  // Gate 판정 기록
};

// ─────────────────────────────────────────────────────────────────────────────
// AgentDeps — AgentLoop 의존성 주입 구조체
// ─────────────────────────────────────────────────────────────────────────────
struct AgentDeps {
  InferenceAdapter*  provider  = nullptr;
  ToolRegistry*      registry  = nullptr;
  PermissionGate*    gate      = nullptr;
  ToolInvoker*       invoker   = nullptr;
  ApprovalStore*     approvals = nullptr;
  BudgetTracker*     budget    = nullptr;
  ConversationStore* conv      = nullptr;
  AuditJournal*      audit     = nullptr;
  const Clock*       clock     = nullptr;
  ContextCompactor*  compactor = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// AgentLoopConfig — 루프 실행 한도 및 설정
// ─────────────────────────────────────────────────────────────────────────────
struct AgentLoopConfig {
  std::size_t   max_turns = 8;
  std::int64_t  approval_timeout_ms = 120000;
  std::size_t   max_action_bytes = 65536;
  int           max_action_depth = 16;
  int           max_reentry = 1;
};

// ─────────────────────────────────────────────────────────────────────────────
// AgentLoop — 코어 오케스트레이션 FSM 루프 엔진
// ─────────────────────────────────────────────────────────────────────────────
class AgentLoop {
 public:
  AgentLoop(AgentDeps deps,
            Subject subject,
            ExecutionMode mode,
            SessionId session_id,
            AgentLoopConfig config = AgentLoopConfig{});

  AgentLoop(const AgentLoop&) = delete;
  AgentLoop& operator=(const AgentLoop&) = delete;

  // 신규 사용자 입력으로 턴 시작
  Result<TurnOutcome> RunTurn(const std::string& user_input);

  // PendingApproval 상태에서 승인 등록 후 턴 재개
  Result<TurnOutcome> ResumeTurn();

  // 비동기 취소 요청 (스레드 안전)
  void RequestCancel() noexcept;

  // 불확실 잠금 수동 승인/해제 (G0-04 / 🟠J)
  Error AcknowledgeIndeterminate(const Subject& operator_subject, const std::string& note);

  // 미완료 턴 엔드 재시도 (G0-06)
  [[nodiscard]] Error RetryFinalize();

  // 세션 봉인 (이후 턴 시작 불가)
  [[nodiscard]] Error SealSession();

  bool sealed() const noexcept { return sealed_; }
  bool is_turn_active() const noexcept { return turn_active_; }
  TurnId current_turn_id() const noexcept { return turn_; }
  const SessionId& session_id() const noexcept { return session_; }
  const Fsm& fsm() const noexcept { return fsm_; }

 private:
  Result<TurnOutcome> Drive();
  Error Finalize(TurnOutcome* out);
  [[nodiscard]] Error Fire(Event ev, const std::string& cause, const ActionId& aid = ActionId{});

  AgentDeps                 d_;
  Subject                   subject_;
  ExecutionMode             mode_;
  SessionId                 session_;
  AgentLoopConfig           config_;
  Fsm                       fsm_;
  TurnId                    turn_ = 0;
  bool                      turn_active_ = false;
  bool                      finalize_pending_ = false;
  bool                      sealed_ = false;
  std::atomic<bool>         cancel_flag_{false};
  std::atomic<bool>         in_call_{false}; // 재진입 및 다중 스레드 호출 가드

  // Action별 Gate 재진입 횟수 (G0-31)
  std::map<ActionId, int>   gate_reentry_;

  // 세션 내 Indeterminate 잠금 (G0-05: operation_digest 기준)
  std::set<std::string>     indeterminate_locks_;
  bool                      line_write_lockdown_ = false;

  // 미결 Action 보관 (PendingApproval 시)
  ActionRequest             pending_action_;
  std::string               pending_approval_id_;
};

}  // namespace cogito

#endif  // COGITO_AGENT_LOOP_HPP