// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 권한 관문 (PermissionGate)
//
// 규범 근거 : Cogito++_구현명세서.md §4-8(:840-868), §6-2(:1480-1615), §6-2-a(:1617-1645),
//             §8-4 [S-2], §2 레이아웃(:81), §3 불변식 1·3·5·6
// G0 결정   : G0-04 (indeterminate lockdown), G0-05 (operation digest), G0-25 (GateInput),
//             G0-30 (FindUsable), G0-31 (gate_reentry_count 상한)
//
// 1~7단계는 순수 판정(Pure Evaluation)이다.
// Evaluate 는 감사 커밋, 상태 변경, 대기, Permit 발급을 절대 수행하지 않는다(명세:1486-1487).
// 판정 결과가 Allow 일 때만 AgentLoop 가 불변 프로토콜에 따라 IssuePermit 을 호출한다.
#ifndef COGITO_PERMISSION_GATE_HPP
#define COGITO_PERMISSION_GATE_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cogito/action.hpp"
#include "cogito/budget.hpp"
#include "cogito/canonical_json.hpp"
#include "cogito/digest.hpp"
#include "cogito/fsm.hpp"
#include "cogito/identity.hpp"
#include "cogito/ids.hpp"
#include "cogito/permit.hpp"
#include "cogito/policy.hpp"
#include "cogito/registry.hpp"
#include "cogito/result.hpp"
#include "cogito/tool.hpp"

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// GateInput — Evaluate 순수 판정에 필요한 스냅샷 입력 (G0-25)
//
// 모든 필드는 호출 시점의 불변 스냅샷이어야 하며 포인터/참조는 const 여야 한다.
// ─────────────────────────────────────────────────────────────────────────────
struct GateInput {
  Digest        action_digest;          // ComputeActionDigest 결과
  Digest        permit_scope_digest;    // ComputePermitScopeDigest 결과
  Digest        operation_digest;       // G0-05: tool_name + CCJ(arguments)
  Subject       subject;                // 요청 주체 (호출자/운영자)
  ExecutionMode mode = ExecutionMode::Default; // 유효 실행 모드
  State         fsm_state = State::Gate;// 현재 FSM 상태 (반드시 State::Gate 여야 함)
  std::string   now_utc;                // 현재 시각 RFC3339 UTC
  std::int64_t  now_ns = 0;             // 현재 단조 시각 (monotonic nanoseconds)
  int           gate_reentry_count = 0; // G0-31: 동일 Action 재진입 횟수 (0부터 시작)
  bool          indeterminate_locked = false; // G0-04: 이전 Indeterminate 불확실 잠금 여부
};

// ─────────────────────────────────────────────────────────────────────────────
// ApprovalRecord — 작업자/관리자의 서명된 승인 레코드 (G0-03)
// ─────────────────────────────────────────────────────────────────────────────
struct ApprovalRecord {
  std::string   approval_id;            // 승인 식별자 (UUIDv4)
  Digest        action_digest;          // 대상 Action Digest
  Digest        scope_digest;           // 대상 Permit Scope Digest
  SessionId     session_id;             // 세션 ID
  TurnId        turn_id = 0;            // 턴 ID
  Subject       requester;              // 승인 요청 주체 (LLM / Agent)
  Subject       approver;               // 승인 수행 주체 (Human Operator / Security Officer)
  std::string   granted_at_utc;         // 승인 시각 RFC3339
  std::int64_t  expires_at_ns = 0;      // 승인 만료 단조 시각
  std::string   nonce;                  // 단일 사용 일회성 난수
  bool          consumed = false;       // 단일 소비 여부
};

// ─────────────────────────────────────────────────────────────────────────────
// ApprovalLookupResult — 승인 검색 결과 및 상세 거부 사유 (G0-03 / ADR-0001)
// ─────────────────────────────────────────────────────────────────────────────
struct ApprovalLookupResult {
  const ApprovalRecord* record = nullptr;
  Error                 status = Error::Ok();
};

// ─────────────────────────────────────────────────────────────────────────────
// ApprovalStore — 승인 레코드 저장소 인터페이스
// ─────────────────────────────────────────────────────────────────────────────
class ApprovalStore {
 public:
  virtual ~ApprovalStore() = default;

  // 유효한 미소비 승인 검색 (만료/소비/다이제스트 대조)
  // - 유효 승인 존재 시: record != nullptr, status.ok() == true
  // - 승인 만료 시: status.reason_code = reason::kApprovalExpired
  // - 승인 기소비 시: status.reason_code = reason::kApprovalAlreadyConsumed
  // - 스코프/다이제스트 불일치: status.reason_code = reason::kApprovalScopeMismatch
  // - 승인 미등록 시: record == nullptr, status = Error{Errc::NotFound, reason::kApprovalRequired, ...}
  virtual const ApprovalRecord* FindUsable(const Digest& action_digest,
                                           const Digest& scope_digest,
                                           const SessionId& session_id,
                                           TurnId turn_id,
                                           std::int64_t now_ns) const = 0;

  virtual ApprovalLookupResult CheckUsable(const Digest& action_digest,
                                           const Digest& scope_digest,
                                           const SessionId& session_id,
                                           TurnId turn_id,
                                           std::int64_t now_ns) const {
    const auto* rec = FindUsable(action_digest, scope_digest, session_id, turn_id, now_ns);
    if (rec != nullptr) {
      return ApprovalLookupResult{rec, Error::Ok()};
    }
    return ApprovalLookupResult{nullptr, Error{Errc::ApprovalRequired, reason::kApprovalRequired, "No usable approval record found"}};
  }

  // 승인 등록
  virtual Error AddApproval(ApprovalRecord record) = 0;

  // 승인 1회 소비 (단일 사용 강제)
  virtual Error Consume(const std::string& approval_id, std::int64_t now_ns) = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// GateLimits — 관문 입력 위생 및 재진입 상한 (명세:1813-1818)
// ─────────────────────────────────────────────────────────────────────────────
struct GateLimits {
  std::size_t max_action_bytes = 65536; // 액션 인자 JSON 최대 바이트 (64 KB)
  int         max_action_depth = 16;    // 액션 인자 JSON 최대 깊이
  int         max_reentry = 1;          // G0-31: 최대 허용 재진입 횟수 (초과 시 Deny)
};

// ─────────────────────────────────────────────────────────────────────────────
// PermissionGate — 8단계 권한 평가 및 실행 허가 발급기
//
// [8단계 판정 파이프라인]
// 1. 입력 위생 (Syntax / Envelope / UTF-8 / Size / Depth)
// 2. 도구 등록 상태 (Tombstone Forbidden vs Absent)
// 3. 도구 인자 스키마 검증 (Draft-7 subset / Pattern timeout)
// 4. FSM 상태 검증 (현재 상태가 정확히 State::Gate 인지)
// 5. 모드 상한 및 보안 정책 (Mode ceiling / Policy match / Tie-breaking)
// 6. 예산 및 리소스 제한 (Deadline / Token / Tool call / Output bytes)
// 7. 작업자 승인 및 락다운 (Approval required / Destructive / Reentry / Indeterminate)
// 8. IssuePermit (Verdict::Allow 판정 시에만 ExecutionPermit 발급)
// ─────────────────────────────────────────────────────────────────────────────
class PermissionGate {
 public:
  PermissionGate(const ToolRegistry& registry,
                 const PolicyEngine& policy,
                 const BudgetTracker& budget,
                 const ApprovalStore* approvals = nullptr,
                 GateLimits limits = GateLimits{})
      : registry_(registry),
        policy_(policy),
        budget_(budget),
        approvals_(approvals),
        limits_(limits) {}

  // 1~7단계 순수 평가 함수 (상태 불변, side-effect 0)
  Verdict Evaluate(const ActionRequest& a, const GateInput& in) const;

  // 8단계 Permit 발급 함수 (Allow 판정 시에만 AgentLoop 가 호출)
  ExecutionPermit IssuePermit(const ToolDescriptor& td,
                              const Digest& action_digest,
                              const Digest& scope_digest,
                              std::int64_t now_ns) const;

  const ToolRegistry& registry() const noexcept { return registry_; }
  const PolicyEngine& policy() const noexcept { return policy_; }
  const BudgetTracker& budget() const noexcept { return budget_; }
  const GateLimits& limits() const noexcept { return limits_; }

 private:
  const ToolRegistry&  registry_;
  const PolicyEngine&  policy_;
  const BudgetTracker& budget_;
  const ApprovalStore* approvals_ = nullptr;
  GateLimits           limits_;
};

}  // namespace cogito

#endif  // COGITO_PERMISSION_GATE_HPP
