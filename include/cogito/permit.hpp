// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 실행 허가(Permit). 불변식 1을 타입으로 강제한다
//
// 규범 근거 : Cogito++_구현명세서.md §4-9(:870-914), §6-1(:1401-1431), §6-2(:1567-1580),
//             §6-2-a(:1585-1615), §8-4 [S-2](:2176-2194)·[T-1](:2196-2210),
//             §2 레이아웃(:74), §3 불변식 1·5·6·11
// G0 결정   : G0-31 (ADR-0001 D4), G0-05 (Accepted)
//
// 생성 주체는 PermissionGate 하나, 소비 주체는 ToolInvoker 하나다(명세:878-879).
// 복사 불가 + 이동만 + 1회 소비이므로, Gate 판정을 거치지 않은 실행 경로나
// 같은 허가로 두 번 실행하는 경로가 타입 수준에서 존재할 수 없다.
// 이 헤더는 직렬화·영속화 API 를 제공하지 않는다 — Permit 이 턴을 넘어 살아남으면
// 불변식 6(실행 직전 Gate 재평가)이 우회된다.
#ifndef COGITO_PERMIT_HPP
#define COGITO_PERMIT_HPP

#include <cstdint>
#include <string>
#include <utility>

#include "cogito/ids.hpp"
#include "cogito/result.hpp"
#include "cogito/tool.hpp"

namespace cogito {

class PermissionGate;   // 유일한 발급자 (명세:1567-1580)
class ToolInvoker;      // 유일한 소비자 (invoker.hpp ToolInvoker::Invoke)

#if defined(COGITO_TESTING)
namespace testing {
struct PermitTestSeam;
}  // namespace testing
#endif

// ─────────────────────────────────────────────────────────────────────────────
// kVerdictTtlNs — Verdict 유효기간 (명세:1463 `v.expires_at_ns = in.now_ns + kVerdictTtlNs`)
//
// [G0-31 / ADR-0001 확정 규약]
//   Verdict TTL = 60초 (60'000'000'000LL ns).
//   선언은 이 헤더가 소유하며, 정의(값)는 src/permit.cpp 에 위치한다.
// ─────────────────────────────────────────────────────────────────────────────
extern const std::int64_t kVerdictTtlNs;

// ─────────────────────────────────────────────────────────────────────────────
// 불변식 5 결합표 — "승인은 action/scope/policy/registry/session/turn/nonce/주체 에
// 결합되고 단일 사용" 이 Permit 의 어느 필드로 실현되는지.
// Permit 은 8개 값을 원문으로 들고 있지 않다. 두 개의 digest 로 **전이적으로** 결합한다.
// 원문을 들지 않는 이유: Permit 이 감사·로그에 흘러도 인자 원문이 남지 않아야 한다(§7-5).
//
//   결합 대상        담는 곳            근거
//   ───────────────────────────────────────────────────────────────────────────
//   action           action_digest_     §6-1 ComputeActionDigest(:1407-1418)
//   session          action_digest_     └ session_id 를 digest 입력에 포함
//   turn             action_digest_     └ turn_id 를 digest 입력에 포함
//   주체(subject_id) scope_digest_      §6-1 ComputePermitScopeDigest(:1419-1431)
//   scope(mode)      scope_digest_      └ ExecutionMode 를 digest 입력에 포함
//   policy           scope_digest_      └ policy_digest 를 digest 입력에 포함
//   registry         scope_digest_      └ registry_digest 를 digest 입력에 포함
//   nonce            ✗ 담지 않는다      §4-8 ApprovalRecord::nonce(:822)가 담고
//                                       ApprovalStore 가 검증한다(§8-4 [S-2] :2192).
//   단일 사용        consumed_ + move-only
//
// ★ 따라서 nonce 재생 검증과 자기승인 거부([S-2] :2194)는 **Permit 계층에서 할 수 없다.**
//   Permit 은 승인자 신원을 담지 않는다. 두 검사는 ApprovalStore::Respond/FindUsable 의
//   책임이며, Permit 은 그 검사를 통과한 뒤에야 발급된다(§6-2-a :1606-1607).
//   이 분리를 흐리지 마라 — Permit 이 승인 권위를 갖는 순간 불변식 10 이 무너진다.
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// ExecutionPermit
//
// @thread: agent-loop-only — 발급·이동·검사·소비가 모두 AgentLoop owner thread 에서
// 일어난다(불변식 11, §8-4 [T-1] :2196). 이 타입은 스레드 안전하지 않으며,
// 다른 스레드로 넘기거나 워커 풀에 보관하지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
class ExecutionPermit {
 public:
  // 복사 금지 — 복사가 가능하면 한 번의 Gate 판정으로 두 번 실행할 수 있다(명세:883-884).
  ExecutionPermit(const ExecutionPermit&)            = delete;
  ExecutionPermit& operator=(const ExecutionPermit&) = delete;

  // 이동만 허용한다(명세:885-886).
  //
  // ★ `= default` 로 두지 마라. 기본 이동은 moved-from `std::string` 을
  //   "유효하지만 미지정" 상태로 남기므로 tool_name_ 이 비지 않을 수 있고,
  //   그러면 moved-from Permit 이 valid() 로 보여 이중 실행 경로가 생긴다.
  //   구현은 이동 후 원본을 반드시 무효 상태로 만든다
  //   (tool_name_ 을 비우고 소비 가능성을 제거한다).
  ExecutionPermit(ExecutionPermit&& o) noexcept;
  ExecutionPermit& operator=(ExecutionPermit&& o) noexcept;

  // 소멸자는 아무것도 되돌리지 않는다(명세:887).
  //   §6-2-a(:1605-1607) 상 `tool_call_started` 커밋과 `approvals.Consume` 은
  //   Permit 발급 **이전**에 이미 끝나 있다. 따라서 미소비 Permit 이 그냥 소멸하면
  //   "허가는 소진됐고 실행은 0회" 가 되며, 이는 fail-closed 로 의도된 결과다.
  //   여기서 승인을 되살리거나 예산을 환급하지 마라.
  ~ExecutionPermit();

  // 미소비이고 발급된 적이 있는가(명세:889).
  // 기본 생성 상태와 moved-from 상태는 tool_name_ 이 비어 있어 false 다.
  bool valid() const noexcept { return !consumed_ && !tool_name_.empty(); }

  const Digest& action_digest() const noexcept { return action_digest_; }
  const Digest& permit_scope_digest() const noexcept { return scope_digest_; }

  const std::string& tool_name() const noexcept { return tool_name_; }

  // monotonic 기준 절대 시각. wall clock 이 아니다(§7-4 process_epoch_id 안에서만 유효).
  std::int64_t expires_ns() const noexcept { return expires_ns_; }
  std::int32_t timeout_ms() const noexcept { return timeout_ms_; }

  Effect effect() const noexcept { return effect_; }

  // lowercase_hex(action_digest) (G0-05 ② :143-146).
  // operation_digest 가 아니다 — 그건 indeterminate 잠금 키이고 세션 수명을 갖는다.
  const std::string& idempotency_key() const noexcept { return idem_key_; }

  // 만료 판정. 경계는 여기서 고정한다 — **만료 시각 정각은 만료로 본다**(fail-closed).
  // 명세에 경계 규정이 없어 한쪽에서 정해야 하며, 실행을 더 허용하는 쪽을 택하지 않는다.
  bool IsExpired(std::int64_t now_ns) const noexcept { return now_ns >= expires_ns_; }

  // ───────────────────────────────────────────────────────────────────────────
  // CheckUsable — invoker.hpp ToolInvoker::Invoke [실행 계약] 1번(valid·미소비·미만료·tool_name 일치·
  // scope 일치)을 한 곳에서 판정한다. 소비하지 않는다(const).
  //
  // ⚠ 명세 §4-9 골격(:881-911)에 없는 **추가 선언**이다. 근거는 invoker.hpp ToolInvoker::Invoke [실행 계약] 1번이며,
  //   `expected_scope_digest` 를 무엇으로 넘길지는 **미결**이다 — invoker.hpp struct ToolCallContext 의
  //   ToolCallContext 에 스코프 digest 필드가 없어 ToolInvoker 가 스스로 구할 수 없다.
  //   호출부(AgentLoop)가 넘기게 할지 ToolCallContext 에 필드를 더할지는 별도 결정이다.
  //   같은 판정을 ToolInvoker 와 AgentLoop 가 각자 재구현하면 두 판정이 갈라진다.
  //   리뷰에서 불필요하다고 판단되면 제거 대상이다.
  //
  // 판정 순서와 reason_code 는 ADR-0001 D4(:86-92)의 FindUsable 순서를 그대로 따른다.
  // 계층이 달라도 같은 상황에 같은 코드가 나와야 사후 조사가 가능하다.
  //   0) 무효(기본 생성·moved-from)  -> Errc::Internal,        reason_code 없음
  //                                     (게이트 판정이 아니라 호출자 계약 위반이다)
  //   1) 만료                        -> Errc::ApprovalInvalid, reason::kApprovalExpired
  //   2) 이미 소비                   -> Errc::ApprovalInvalid, reason::kApprovalAlreadyConsumed
  //   3) tool_name 불일치            -> Errc::ApprovalInvalid, reason::kApprovalScopeMismatch
  //   4) scope digest 불일치         -> Errc::ApprovalInvalid, reason::kApprovalScopeMismatch
  //   성공                           -> Error::Ok()
  //
  // ★ 통과는 실행 허가가 아니다. 소비는 ToolInvoker 가 handler 호출 '직전'에
  //   정확히 한 번 수행한다(invoker.hpp ToolInvoker::Invoke [실행 계약] 2).
  // ★ 이 함수는 Gate 재평가를 대신하지 않는다(불변식 6). 재평가는 §6-2-a 상
  //   Permit 발급 **이전**에 이미 끝나 있어야 한다.
  //
  // @thread: agent-loop-only
  [[nodiscard]] Error CheckUsable(const std::string& expected_tool_name,
                                  const Digest& expected_scope_digest,
                                  std::int64_t now_ns) const;

 private:
  friend class PermissionGate;   // 유일한 생성자 (명세:899)
  friend class ToolInvoker;      // 유일한 소비자 (명세:900)
#if defined(COGITO_TESTING)
  friend struct testing::PermitTestSeam;  // 단위 테스트용 Seam
#endif

  // 공개 생성 경로를 만들지 마라(불변식 1). Gate 밖에서는 Permit 을 만들 수 없다.
  ExecutionPermit() = default;

  // 소비는 되돌릴 수 없다. 되돌릴 수 있으면 단일 사용 계약이 무의미해진다.
  void Consume() noexcept { consumed_ = true; }

  Digest       action_digest_{}, scope_digest_{};
  std::string  tool_name_, idem_key_;
  std::int64_t expires_ns_ = 0;
  std::int32_t timeout_ms_ = 0;

  // 기본값은 가장 제한적인 쪽이다(명세:909). 발급 시 td.effect 로 덮어쓰지 못한
  // 경로가 있어도 취소·타임아웃이 Cancelled 가 아니라 Indeterminate 로 분류된다
  // (invoker.hpp ToolInvoker::Invoke [실행 계약] 6·7). 안전한 쪽으로 틀리게 만든다.
  Effect       effect_ = Effect::Destructive;

  bool         consumed_ = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// testing::PermitTestSeam — S4 단위 테스트용 발급 Seam
// ─────────────────────────────────────────────────────────────────────────────
#if defined(COGITO_TESTING)
namespace testing {
struct PermitTestSeam {
  static ExecutionPermit Create(Digest action_digest,
                                Digest scope_digest,
                                std::string tool_name,
                                std::string idempotency_key,
                                std::int64_t expires_ns,
                                std::int32_t timeout_ms,
                                Effect effect,
                                bool consumed = false) {
    ExecutionPermit p;
    p.action_digest_ = std::move(action_digest);
    p.scope_digest_ = std::move(scope_digest);
    p.tool_name_ = std::move(tool_name);
    p.idem_key_ = std::move(idempotency_key);
    p.expires_ns_ = expires_ns;
    p.timeout_ms_ = timeout_ms;
    p.effect_ = effect;
    p.consumed_ = consumed;
    return p;
  }

  static void Consume(ExecutionPermit& p) noexcept {
    p.Consume();
  }
};
}  // namespace testing
#endif

}  // namespace cogito

#endif  // COGITO_PERMIT_HPP
