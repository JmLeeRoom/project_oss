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

inline Result<Decision> ParseDecision(std::string_view sv) noexcept {
  if (sv == "deny" || sv == "Deny") {
    return Decision::Deny;
  }
  if (sv == "ask" || sv == "Ask") {
    return Decision::Ask;
  }
  if (sv == "allow" || sv == "Allow") {
    return Decision::Allow;
  }
  return Error{Errc::InvalidArgument, "", "Unknown decision type"};
}

// ─────────────────────────────────────────────────────────────────────────────
// Verdict — 게이트 판정 세부 결과
// ─────────────────────────────────────────────────────────────────────────────
struct Verdict {
  Decision     decision = Decision::Deny;    // 기본값: 항상 Deny (fail-closed)
  int          gate_stage = 0;               // 거부/판정된 게이트 단계 (1..9)
  std::string  reason_code;                  // reason::* 식별자
  std::string  reason;                       // 운영자/사용자 표시용 메시지
  std::string  rule_id;                      // 일치된 규칙 ID (충돌 시 "")
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
  std::string             tool = "*";               // 대상 도구 이름/와일드카드 패턴 (예: "motor.*", "*")
  std::optional<Effect>   effect_min;               // 하한 부수효과
  std::optional<Effect>   effect_max;               // 상한 부수효과
  std::vector<std::string> modes;                   // 적용 대상 모드 (예: {"*"}, {"plan", "readonly"})
  std::vector<std::string> roles;                   // 적용 대상 역할 (비어있으면 전체)
  Decision                decision = Decision::Deny;
  std::string             reason;                   // 판정 사유 설명
  ccj::Json               constraints;              // 인자 제약 조건 (JSON 객체)
};

// Policy Digest 산출을 위한 규칙 정규화 (ADR-0004 D5 / D8)
// - 누락 슬롯을 항상 고정 기본값(빈 문자열 "", 빈 배열 [], 빈 객체 {})으로 포함하여 슬롯 결정론 보장
inline ccj::Json NormalizePolicyRule(const PolicyRule& rule) {
  ccj::Json j = ccj::Json::object();
  j["constraints"] = (rule.constraints.is_null() || !rule.constraints.is_object())
                         ? ccj::Json::object()
                         : rule.constraints;
  j["decision"] = ToString(rule.decision);
  j["effect_max"] = rule.effect_max.has_value() ? ToString(*rule.effect_max) : "";
  j["effect_min"] = rule.effect_min.has_value() ? ToString(*rule.effect_min) : "";
  j["id"] = rule.id;
  j["modes"] = rule.modes;
  j["priority"] = rule.priority;
  j["reason"] = rule.reason;
  j["roles"] = rule.roles;
  j["tool"] = rule.tool;
  return j;
}

// ─────────────────────────────────────────────────────────────────────────────
// PolicyEngine — 보안 정책 엔진
//
// [평가 및 매칭 규칙]
// 1. 도구 패턴 매칭:
//    - "*" : 모든 도구 일치
//    - "prefix.*" : "prefix." 로 시작하는 모든 도구 일치
//    - 정확한 문자열 : 완전 일치
// 2. 인자 제약조건(constraints) 매칭:
//    - constraints 가 null 이거나 빈 객체이면 모든 인자 일치
//    - constraints 가 객체인 경우, constraints 에 명시된 모든 키가 args 에 존재하고
//      값이 완전 일치(equality)해야 함. args 가 객체가 아니거나 키/값이 다르면 불일치.
// 3. 모드 매칭:
//    - rule.modes 가 비어있거나 "*" 를 포함하면 모든 모드 일치
//    - 그렇지 않으면 현재 mode(소문자 문자열)가 rule.modes 목록에 포함되어야 함
// 4. 역할(roles) 매칭:
//    - rule.roles 가 비어있으면 모든 주체 일치
//    - 그렇지 않으면 subject.roles 중 최소 1개 이상이 rule.roles 에 포함되어야 함
// 5. 부수효과(effect) 매칭:
//    - effect_min 지정 시: tool_effect >= *effect_min
//    - effect_max 지정 시: tool_effect <= *effect_max
// 6. 최고 우선순위 규칙 수집 및 동률 충돌 해결 (Tie-breaking):
//    - 일치하는 모든 규칙 중 priority 가 가장 높은 규칙 집합을 선택
//    - [충돌 발생]: 집합 내 decision 이 서로 다를 경우
//        - 가장 안전한 decision 을 채택 (Deny > Ask > Allow)
//        - rule_id = "" (충돌로 인한 판정이므로 특정 규칙 단독 ID 미지정)
//        - [Deny 로 해결 (Deny vs Ask, Deny vs Allow, Deny vs Ask vs Allow)]:
//            - reason_code = reason::kPolicyConflictDenied
//            - reason = "Policy conflict: multiple rules matched with differing decisions, resolved to Deny"
//        - [Ask 로 해결 (Ask vs Allow)]:
//            - reason_code = reason::kApprovalRequired
//            - reason = "Policy conflict: multiple rules matched with differing decisions, resolved to Ask"
//    - [결정 일치]: 집합 내 decision 이 모두 동일한 경우
//        - id 가 사전순(UTF-8 바이트 오름차순)으로 가장 작은 규칙을 최종 승리 규칙으로 선정
//        - rule_id = winning_rule.id, reason = winning_rule.reason
//        - reason_code = (decision == Decision::Ask) ? reason::kApprovalRequired
//                      : ((decision == Decision::Allow) ? reason::kAllowed : reason::kPolicyDenied)
// 7. 모드 상한 검사 (G0-29 / Gate 5단계 연계):
//    - tool_effect > ModeToMaxEffect(mode) 인 경우 정책 규칙과 무관하게 강제 Deny
//    - decision = Decision::Deny, gate_stage = 5, reason_code = reason::kModeDenied,
//      reason = "Tool effect exceeds execution mode upper bound"
// 8. 일치 규칙이 없는 경우:
//    - default_decision (기본 Deny) 반환, reason_code = reason::kNoMatchingRule,
//      reason = "No matching policy rule found"
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
