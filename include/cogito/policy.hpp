// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 보안 정책 및 판정 계약 (Decision, Verdict, PolicyRule, PolicyEngine)
//
// 규범 근거 : Cogito++_구현명세서.md §4-7(:728-750), §4-12, 체크리스트 S2-07, G0-29
#ifndef COGITO_POLICY_HPP
#define COGITO_POLICY_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cogito/canonical_json.hpp"
#include "cogito/digest.hpp"
#include "cogito/identity.hpp"
#include "cogito/ids.hpp"
#include "cogito/result.hpp"
#include "cogito/tool.hpp"

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// Decision — 판정 결과 (Allow, Ask, Deny)
// ─────────────────────────────────────────────────────────────────────────────
enum class Decision : std::uint8_t {
  Deny = 0,   // 기본값: 안전을 위해 0 = Deny
  Ask  = 1,   // 사람 승인 필요
  Allow = 2   // 즉시 실행 허용
};

inline const char* ToString(Decision d) noexcept {
  switch (d) {
    case Decision::Deny:  return "deny";
    case Decision::Ask:   return "ask";
    case Decision::Allow: return "allow";
  }
  return "deny";
}

// ─────────────────────────────────────────────────────────────────────────────
// Verdict — 게이트 판정 세부 결과
// ─────────────────────────────────────────────────────────────────────────────
struct Verdict {
  Decision     decision = Decision::Deny;    // 기본값: 항상 Deny (fail-closed)
  int          gate_stage = 0;               // 거부/판정된 게이트 단계 (1..9)
  std::string  reason_code;                  // reason::* 식별자
  std::string  reason;                       // 운영자/사용자 표시용 메시지
  std::string  rule_id;                      // 일치된 규칙 ID
  Digest       policy_digest{};
  Digest       registry_digest{};
  Digest       action_digest{};
  std::string  evaluated_at_utc;
  std::int64_t expires_at_ns = 0;            // monotonic ns 기준 만료 시점
};

// ─────────────────────────────────────────────────────────────────────────────
// PolicyRule — config/policy.schema.json 과 1:1 대응되는 단일 규칙 구조체
// ─────────────────────────────────────────────────────────────────────────────
struct PolicyRule {
  std::string             id;                       // 고유 규칙 ID
  std::uint64_t           priority = 0;             // 높을수록 우선 적용
  std::string             tool = "*";               // 대상 도구 이름/와일드카드 패턴 (예: "motor.*")
  std::optional<Effect>   effect_min;               // 하한 부수효과
  std::optional<Effect>   effect_max;               // 상한 부수효과
  std::vector<std::string> modes;                   // 적용 대상 모드 (예: {"*"}, {"plan", "readonly"})
  std::vector<std::string> roles;                   // 적용 대상 역할 (비어있으면 전체)
  Decision                decision = Decision::Deny;
  std::string             reason;                   // 판정 사유 설명
  ccj::Json               constraints;              // 인자 제약 조건 (JSON)
};

// ─────────────────────────────────────────────────────────────────────────────
// PolicyEngine — 보안 정책 엔진
//
// [평가 규칙]
// 1. 규칙 정렬: priority 내림차순(높은 우선순위 우선)
// 2. 동률 규칙 충돌 시: Deny > Ask > Allow 순으로 안전한 결정을 취하며 reason::kPolicyConflictDenied 기록
// 3. 모드 상한 검사: 도구의 Effect 가 ModeToMaxEffect(mode) 보다 크면 강제 Deny (reason::kModeDenied)
// 4. 일치 규칙이 없으면 default_decision (기본 Deny) 반환 (reason::kNoMatchingRule)
// ─────────────────────────────────────────────────────────────────────────────
class PolicyEngine {
 public:
  virtual ~PolicyEngine() = default;

  // JSON 설정 객체로부터 PolicyEngine 생성 및 policy_digest 산출
  static Result<std::unique_ptr<PolicyEngine>> Create(const ccj::Json& policy_json);

  // 요청에 대한 정책 평가
  virtual Verdict Evaluate(const std::string& tool_name,
                           Effect tool_effect,
                           const ccj::Json& args,
                           const Subject& subject,
                           ExecutionMode mode) const = 0;

  virtual const Digest& policy_digest() const noexcept = 0;
  virtual std::uint64_t schema_version() const noexcept = 0;
  virtual const std::string& default_decision() const noexcept = 0;
};

}  // namespace cogito

#endif  // COGITO_POLICY_HPP
