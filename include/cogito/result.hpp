// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 오류·결과 계약
//
// 규범 근거 : Cogito++_구현명세서.md §4-1, §3-4
// G0 결정   : G0-09 (S0/S1 구현 기준 한정 승인 완료)
//
// [G0-09 확정 규칙]
//   R1. 무값 성공을 반환하는 공개 API 는 `Error` 를 쓴다. `Result<void>` 를 쓰지 않는다.
//   R2. `Result<void>` 특수화는 제공하되, 템플릿 매개변수로 void 가 올 수 있는
//       제네릭 코드 전용이다. 공개 API 시그니처에 직접 쓰지 않는다.
//   R3. 예외: 코어 내부는 허용. C ABI 경계를 넘지 않는다. 도구 핸들러 예외는
//       ToolInvoker 가 자체적으로 잡는다. 감사 경로의 OOM 은 fail-closed.
//   R4. 실패 Result 에서 value() 를 호출하면 Release 에서도 std::terminate 로 중단한다.
//       assert 에 의존하지 않는다. 조용한 UB 를 남기지 않는다.
#ifndef COGITO_RESULT_HPP
#define COGITO_RESULT_HPP

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// Errc — 내부 오류 분류. C ABI 의 cogito_status_t 로 사상된다(§8-2).
// 값은 안정적이다. 재배치·재사용을 금지한다.
// ─────────────────────────────────────────────────────────────────────────────
enum class Errc : std::int32_t {
  Ok = 0,

  // 입력·계약
  InvalidArgument,
  NotRegistered,
  Forbidden,
  SchemaViolation,
  SchemaCompileFailed,
  ToolContractViolation,

  // 게이트
  PolicyDenied,
  ApprovalRequired,
  ApprovalInvalid,
  BudgetExhausted,
  DeadlineExceeded,
  Cancelled,

  // 실행
  ProviderError,
  ProviderContractViolation,
  ToolError,
  Indeterminate,

  // 감사
  AuditWriteFailed,
  AuditChainBroken,

  // 파싱·인코딩
  DuplicateKey,
  NotUtf8,
  TooLarge,
  DepthExceeded,

  // 설정·수명
  ConfigError,
  SecretError,
  TurnSealed,
  WrongThread,        // §8-4 [T-1] — agent-loop-only 함수를 다른 스레드에서 호출

  Internal = 99
};

// ─────────────────────────────────────────────────────────────────────────────
// reason_code — 사용자 표시 문자열과 분리된 안정 식별자(§3-4).
// major 버전 내에서 불변이다. UI·감사·테스트는 표시 문구가 아니라 이 코드로 분기한다.
// ─────────────────────────────────────────────────────────────────────────────
namespace reason {

// 허용
inline constexpr const char* kAllowed = "allowed";

// 1단계 — 입력 위생
inline constexpr const char* kInputTooLarge      = "input_too_large";
inline constexpr const char* kInputDepthExceeded = "input_depth_exceeded";
inline constexpr const char* kInputNotUtf8       = "input_not_utf8";
inline constexpr const char* kInputMissingField  = "input_missing_field";
inline constexpr const char* kInputDuplicateKey  = "input_duplicate_key";

// 2단계 — 등록
inline constexpr const char* kToolNotRegistered = "tool_not_registered";
inline constexpr const char* kToolForbidden     = "tool_forbidden";

// 3단계 — 스키마
inline constexpr const char* kSchemaViolation = "schema_violation";
inline constexpr const char* kPatternTimeout  = "pattern_timeout";

// 4단계 — FSM 상태
inline constexpr const char* kInvalidFsmState = "invalid_fsm_state";

// 5단계 — 모드·역할·정책
inline constexpr const char* kPolicyDenied         = "policy_denied";
inline constexpr const char* kModeDenied           = "mode_denied";
inline constexpr const char* kRoleDenied           = "role_denied";
inline constexpr const char* kPolicyConflictDenied = "policy_conflict_denied";
inline constexpr const char* kNoMatchingRule       = "no_matching_rule";

// 6단계 — 예산
inline constexpr const char* kBudgetTokens    = "budget_tokens";
inline constexpr const char* kBudgetToolCalls = "budget_tool_calls";
inline constexpr const char* kBudgetRepeat    = "budget_repeat";
inline constexpr const char* kBudgetDeadline  = "budget_deadline";

// 7단계 — 승인.  ADR-0001 D4 의 판정 순서대로 구분한다(뭉개면 사후 조사 불가).
inline constexpr const char* kApprovalRequired        = "approval_required";
inline constexpr const char* kApprovalRejected        = "approval_rejected";
inline constexpr const char* kApprovalExpired         = "approval_expired";
inline constexpr const char* kApprovalAlreadyConsumed = "approval_already_consumed";
inline constexpr const char* kApprovalScopeMismatch   = "approval_scope_mismatch";
inline constexpr const char* kApprovalReentryExceeded = "approval_reentry_exceeded";
inline constexpr const char* kApprovalSelfApproval    = "approval_self_approval";
inline constexpr const char* kApprovalNonceMismatch   = "approval_nonce_mismatch";

// 8단계 — 감사
inline constexpr const char* kAuditCommitFailed = "audit_commit_failed";

// 강등 — operation_digest 기준으로 판정한다(ADR-0004 D7)
inline constexpr const char* kIndeterminateLockdown = "indeterminate_lockdown";

}  // namespace reason

// ─────────────────────────────────────────────────────────────────────────────
// Error
// ─────────────────────────────────────────────────────────────────────────────
struct Error {
  Errc code = Errc::Ok;

  // reason:: 상수 중 하나. 게이트 판정이 아닌 오류는 빈 문자열을 허용한다.
  std::string reason_code;

  // 사용자 표시용(한국어). 이 문자열로 분기하지 않는다.
  std::string message;

  // 진단용. 감사 payload 에 넣을 때는 §7-5 마스킹을 거친다.
  // 비밀값·도구 인자 원문을 여기에 자동으로 담지 않는다(체크리스트 S1-01).
  std::string detail;

  // 오류가 있으면 true. Error::Ok() 는 false.
  explicit operator bool() const noexcept { return code != Errc::Ok; }

  bool ok() const noexcept { return code == Errc::Ok; }

  static Error Ok() noexcept { return Error{}; }
};

namespace detail {
// R4 — 잘못된 접근은 Release 에서도 정의된 동작(중단)을 갖는다.
[[noreturn]] inline void ResultAccessViolation() noexcept { std::terminate(); }
}  // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// Result<T>
//   - 성공 객체에서만 value()/take() 접근 가능
//   - 실패 객체에서만 error() 가 의미를 가짐
//   - move-only 타입도 담을 수 있다
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
class [[nodiscard]] Result {
 public:
  static_assert(!std::is_reference<T>::value, "Result<T&> 는 지원하지 않는다");
  static_assert(!std::is_void<T>::value, "무값 성공은 Error 를 쓴다 (G0-09 R1)");

  // 암시적 변환을 허용한다 — `return value;` / `return error;` 를 자연스럽게 쓰기 위함.
  Result(T v) : value_(std::move(v)) {}                    // NOLINT(google-explicit-constructor)
  Result(Error e) : error_(std::move(e)) {                 // NOLINT(google-explicit-constructor)
    if (error_.ok()) detail::ResultAccessViolation();      // Ok 인 Error 로 실패를 만들 수 없다
  }

  Result(const Result&) = default;
  Result& operator=(const Result&) = default;
  Result(Result&&) noexcept = default;
  Result& operator=(Result&&) noexcept = default;

  bool ok() const noexcept { return error_.ok(); }
  explicit operator bool() const noexcept { return ok(); }

  const T& value() const& {
    if (!ok()) detail::ResultAccessViolation();
    return *value_;
  }
  T& value() & {
    if (!ok()) detail::ResultAccessViolation();
    return *value_;
  }
  T&& take() && {
    if (!ok()) detail::ResultAccessViolation();
    return std::move(*value_);
  }
  // lvalue 에서의 take() — 호출 후 이 객체의 값은 moved-from 이다.
  T&& take() & {
    if (!ok()) detail::ResultAccessViolation();
    return std::move(*value_);
  }

  const Error& error() const noexcept { return error_; }

 private:
  std::optional<T> value_;
  Error error_{};
};

// ─────────────────────────────────────────────────────────────────────────────
// Result<void> — G0-09 R2. 제네릭 코드 전용.
// 공개 API 시그니처에 직접 쓰지 않는다. Error 와 상호 변환된다.
// ─────────────────────────────────────────────────────────────────────────────
template <>
class [[nodiscard]] Result<void> {
 public:
  Result() = default;                                       // 성공
  // Result<T> 와 달리 Ok 인 Error 를 허용한다 — 무값 결과에서는 Error 자체가 페이로드이고
  // Error::Ok() 는 '성공'을 뜻하기 때문이다. 이 비대칭은 의도된 것이다.
  Result(Error e) : error_(std::move(e)) {}                 // NOLINT(google-explicit-constructor)

  bool ok() const noexcept { return error_.ok(); }
  explicit operator bool() const noexcept { return ok(); }

  void value() const {
    if (!ok()) detail::ResultAccessViolation();
  }

  const Error& error() const noexcept { return error_; }

  // Error 로의 명시적 환원 — 공개 API 경계에서 사용한다.
  Error ToError() const { return error_; }

  static Result<void> Success() noexcept { return Result<void>{}; }

 private:
  Error error_{};
};

}  // namespace cogito

#endif  // COGITO_RESULT_HPP
