// SPDX-License-Identifier: Apache-2.0

#include "cogito/policy.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>

namespace cogito {
namespace {

Error PolicySchemaError(std::string message) {
  return Error{Errc::SchemaViolation, reason::kSchemaViolation, std::move(message)};
}

Error PolicyInternalError(const char* message) {
  return Error{Errc::Internal, {}, message};
}

bool IsKnownEffect(Effect effect) noexcept {
  switch (effect) {
    case Effect::None:
    case Effect::Write:
    case Effect::Destructive:
      return true;
  }
  return false;
}

bool IsKnownMode(ExecutionMode mode) noexcept {
  switch (mode) {
    case ExecutionMode::Default:
    case ExecutionMode::Plan:
    case ExecutionMode::Edit:
    case ExecutionMode::ReadOnly:
      return true;
  }
  return false;
}

bool IsValidToolPattern(std::string_view pattern) noexcept {
  if (pattern == "*") {
    return true;
  }
  const std::size_t wildcard = pattern.find('*');
  if (wildcard == std::string_view::npos) {
    return true;
  }
  return wildcard + 1U == pattern.size() && wildcard >= 2U &&
         pattern[wildcard - 1U] == '.' &&
         pattern.find('*', wildcard + 1U) == std::string_view::npos;
}

bool Utf8ByteLess(std::string_view lhs, std::string_view rhs) noexcept {
  return std::lexicographical_compare(
      lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
      [](char left, char right) {
        return static_cast<unsigned char>(left) <
               static_cast<unsigned char>(right);
      });
}

Result<Effect> ParseEffect(std::string_view sv) noexcept {
  if (sv == "none") {
    return Effect::None;
  }
  if (sv == "write") {
    return Effect::Write;
  }
  if (sv == "destructive") {
    return Effect::Destructive;
  }
  return Error{Errc::InvalidArgument, "", "Unknown effect type"};
}

const ccj::Json& GetPolicySchema() {
  static const ccj::Json kSchema = ccj::Json::parse(R"json({
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "CogitoPolicySchema",
    "description": "보안 정책 스키마 (Draft-07)",
    "type": "object",
    "properties": {
      "schema_version": {
        "type": "integer",
        "minimum": 1,
        "maximum": 1
      },
      "default": {
        "type": "string",
        "enum": ["deny", "ask", "allow"],
        "default": "deny"
      },
      "rules": {
        "type": "array",
        "items": {
          "type": "object",
          "properties": {
            "id": {
              "type": "string",
              "minLength": 1,
              "maxLength": 128
            },
            "priority": {
              "type": "integer",
              "minimum": 0,
              "maximum": 100000
            },
            "tool": {
              "type": "string",
              "minLength": 1,
              "maxLength": 128
            },
            "effect_min": {
              "type": "string",
              "enum": ["none", "write", "destructive"]
            },
            "effect_max": {
              "type": "string",
              "enum": ["none", "write", "destructive"]
            },
            "modes": {
              "type": "array",
              "items": {
                "type": "string",
                "enum": ["*", "default", "plan", "edit", "readonly"]
              }
            },
            "roles": {
              "type": "array",
              "items": {
                "type": "string",
                "minLength": 1,
                "maxLength": 64
              }
            },
            "decision": {
              "type": "string",
              "enum": ["deny", "ask", "allow"]
            },
            "reason": {
              "type": "string",
              "maxLength": 512
            },
            "constraints": {
              "type": "object"
            }
          },
          "required": ["id", "priority", "tool", "decision"],
          "additionalProperties": false
        }
      }
    },
    "required": ["schema_version", "default", "rules"],
    "additionalProperties": false
  })json");
  return kSchema;
}

const nlohmann::json_schema::json_validator& GetPolicyValidator() {
  static const auto kValidator = []() {
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(GetPolicySchema());
    return validator;
  }();
  return kValidator;
}

class PolicyEngineImpl : public PolicyEngine {
 public:
  PolicyEngineImpl(std::uint64_t schema_version,
                   std::string default_decision,
                   Decision default_decision_enum,
                   std::vector<PolicyRule> rules,
                   Digest policy_digest)
      : schema_version_(schema_version),
        default_decision_(std::move(default_decision)),
        default_decision_enum_(default_decision_enum),
        rules_(std::move(rules)),
        policy_digest_(std::move(policy_digest)) {}

  Verdict Evaluate(const std::string& tool_name,
                   Effect tool_effect,
                   const ccj::Json& args,
                   const Subject& subject,
                   ExecutionMode mode) const override {
    Verdict v;
    v.policy_digest = policy_digest_;
    v.gate_stage = 5;
    v.decision = Decision::Deny;
    v.rule_id = "";

    // 1. Gate 5 모드 상한 검사 (G0-29)
    if (!IsKnownEffect(tool_effect) || !IsKnownMode(mode) ||
        tool_effect > ModeToMaxEffect(mode)) {
      v.decision = Decision::Deny;
      v.gate_stage = 5;
      v.reason_code = reason::kModeDenied;
      v.reason = "Tool effect exceeds execution mode upper bound";
      return v;
    }

    // 2. 규칙 매칭
    std::vector<const PolicyRule*> matched_rules;
    matched_rules.reserve(rules_.size());

    const std::string current_mode_str = ToString(mode);

    for (const auto& rule : rules_) {
      // (1) 도구 패턴 매칭
      if (rule.tool != "*") {
        if (rule.tool.size() >= 2 && rule.tool.back() == '*' && rule.tool[rule.tool.size() - 2] == '.') {
          const std::string_view prefix = std::string_view(rule.tool).substr(0, rule.tool.size() - 1);
          if (tool_name.rfind(prefix, 0) != 0) {
            continue;
          }
        } else {
          if (rule.tool != tool_name) {
            continue;
          }
        }
      }

      // (2) 부수효과 범위 매칭
      if (rule.effect_min.has_value() && tool_effect < *rule.effect_min) {
        continue;
      }
      if (rule.effect_max.has_value() && tool_effect > *rule.effect_max) {
        continue;
      }

      // (3) 모드 매칭
      if (!rule.modes.empty()) {
        bool mode_ok = false;
        for (const auto& m : rule.modes) {
          if (m == "*" || m == current_mode_str) {
            mode_ok = true;
            break;
          }
        }
        if (!mode_ok) {
          continue;
        }
      }

      // (4) 역할 매칭
      if (!rule.roles.empty()) {
        bool role_ok = false;
        for (const auto& s_role : subject.roles) {
          for (const auto& r_role : rule.roles) {
            if (s_role == r_role) {
              role_ok = true;
              break;
            }
          }
          if (role_ok) {
            break;
          }
        }
        if (!role_ok) {
          continue;
        }
      }

      // (5) 인자 제약조건(constraints) 매칭
      if (!rule.constraints.is_null() && rule.constraints.is_object() && !rule.constraints.empty()) {
        if (!args.is_object()) {
          continue;
        }
        bool constraints_ok = true;
        for (auto it = rule.constraints.begin(); it != rule.constraints.end(); ++it) {
          auto arg_it = args.find(it.key());
          if (arg_it == args.end() || *arg_it != it.value()) {
            constraints_ok = false;
            break;
          }
        }
        if (!constraints_ok) {
          continue;
        }
      }

      matched_rules.push_back(&rule);
    }

    // 3. 일치 규칙이 없는 경우
    if (matched_rules.empty()) {
      v.decision = default_decision_enum_;
      v.reason_code = reason::kNoMatchingRule;
      v.reason = "No matching policy rule found";
      v.rule_id = "";
      return v;
    }

    // 4. 최고 우선순위 규칙 집합 추출
    std::uint64_t max_priority = 0;
    for (const auto* r : matched_rules) {
      max_priority = std::max(max_priority, r->priority);
    }

    std::vector<const PolicyRule*> top_rules;
    top_rules.reserve(matched_rules.size());
    for (const auto* r : matched_rules) {
      if (r->priority == max_priority) {
        top_rules.push_back(r);
      }
    }

    // 5. 충돌 판별 및 동률 해결
    bool has_deny = false;
    bool has_ask = false;
    bool has_allow = false;

    for (const auto* r : top_rules) {
      if (r->decision == Decision::Deny) {
        has_deny = true;
      } else if (r->decision == Decision::Ask) {
        has_ask = true;
      } else if (r->decision == Decision::Allow) {
        has_allow = true;
      }
    }

    const int distinct_decisions = (has_deny ? 1 : 0) + (has_ask ? 1 : 0) + (has_allow ? 1 : 0);

    if (distinct_decisions > 1) {
      // 충돌 발생: Deny > Ask > Allow 순으로 안전한 결정 채택, rule_id = ""
      v.rule_id = "";
      if (has_deny) {
        v.decision = Decision::Deny;
        v.reason_code = reason::kPolicyConflictDenied;
        v.reason = "Policy conflict: multiple rules matched with differing decisions, resolved to Deny";
      } else {
        // Ask vs Allow 충돌
        v.decision = Decision::Ask;
        v.reason_code = reason::kApprovalRequired;
        v.reason = "Policy conflict: multiple rules matched with differing decisions, resolved to Ask";
      }
      return v;
    }

    // 결정 일치: id 가 사전순(UTF-8 바이트 오름차순)으로 가장 작은 규칙 채택
    const PolicyRule* winner = top_rules[0];
    for (std::size_t i = 1; i < top_rules.size(); ++i) {
      if (Utf8ByteLess(top_rules[i]->id, winner->id)) {
        winner = top_rules[i];
      }
    }

    v.decision = winner->decision;
    v.rule_id = winner->id;
    v.reason = winner->reason;
    if (v.decision == Decision::Ask) {
      v.reason_code = reason::kApprovalRequired;
    } else if (v.decision == Decision::Allow) {
      v.reason_code = reason::kAllowed;
    } else {
      v.reason_code = reason::kPolicyDenied;
    }

    return v;
  }

  const Digest& policy_digest() const noexcept override { return policy_digest_; }
  std::uint64_t schema_version() const noexcept override { return schema_version_; }
  const std::string& default_decision() const noexcept override { return default_decision_; }

 private:
  std::uint64_t            schema_version_ = 1;
  std::string              default_decision_ = "deny";
  Decision                 default_decision_enum_ = Decision::Deny;
  std::vector<PolicyRule>  rules_;
  Digest                   policy_digest_{};
};

}  // namespace

Result<std::unique_ptr<PolicyEngine>> PolicyEngine::Create(const ccj::Json& policy_json) {
  try {
    try {
      GetPolicyValidator().validate(policy_json);
    } catch (const std::bad_alloc&) {
      throw;
    } catch (const std::exception&) {
      return PolicySchemaError("Policy schema validation failed");
    } catch (...) {
      return PolicySchemaError("Policy schema validation failed");
    }

    if (!policy_json.contains("schema_version") ||
        !policy_json["schema_version"].is_number_integer()) {
      return PolicySchemaError("schema_version must be an integer");
    }
    const auto schema_version = policy_json["schema_version"].get<std::uint64_t>();
    if (schema_version != 1U) {
      return PolicySchemaError("unsupported policy schema_version");
    }

    if (!policy_json.contains("default") || !policy_json["default"].is_string()) {
      return PolicySchemaError("default decision must be a string");
    }
    const std::string default_decision_str = policy_json["default"].get<std::string>();
    auto def_dec_res = ParseDecision(default_decision_str);
    if (!def_dec_res) {
      return def_dec_res.error();
    }

    if (!policy_json.contains("rules") || !policy_json["rules"].is_array()) {
      return PolicySchemaError("rules must be an array");
    }

    std::vector<PolicyRule> rules;
    std::vector<PolicyRuleProjectionDto> projections;
    std::unordered_set<std::string> rule_ids;

    for (const auto& rule_json : policy_json["rules"]) {
      PolicyRule rule;
      rule.id = rule_json["id"].get<std::string>();
      if (rule.id.empty() || rule.id.size() > 128U) {
        return PolicySchemaError("rule id length must be 1..128");
      }
      if (!rule_ids.insert(rule.id).second) {
        return Error{Errc::DuplicateKey, reason::kSchemaViolation,
                     "duplicate policy rule id: " + rule.id};
      }

      rule.priority = rule_json["priority"].get<std::uint64_t>();
      rule.tool = rule_json["tool"].get<std::string>();
      if (rule.tool.empty() || rule.tool.size() > 128U) {
        return PolicySchemaError("tool length must be 1..128");
      }
      if (!IsValidToolPattern(rule.tool)) {
        return PolicySchemaError(
            "tool pattern must be exact, '*', or a dotted prefix glob");
      }

      auto dec_res = ParseDecision(rule_json["decision"].get<std::string>());
      if (!dec_res) {
        return dec_res.error();
      }
      rule.decision = dec_res.value();

      if (rule_json.contains("effect_min") && rule_json["effect_min"].is_string()) {
        auto eff_min_res = ParseEffect(rule_json["effect_min"].get<std::string>());
        if (!eff_min_res) {
          return eff_min_res.error();
        }
        rule.effect_min = eff_min_res.value();
      }

      if (rule_json.contains("effect_max") && rule_json["effect_max"].is_string()) {
        auto eff_max_res = ParseEffect(rule_json["effect_max"].get<std::string>());
        if (!eff_max_res) {
          return eff_max_res.error();
        }
        rule.effect_max = eff_max_res.value();
      }

      if (rule.effect_min.has_value() && rule.effect_max.has_value() &&
          *rule.effect_min > *rule.effect_max) {
        return PolicySchemaError(
            "effect_min cannot be greater than effect_max in rule: " + rule.id);
      }

      if (rule_json.contains("modes") && rule_json["modes"].is_array()) {
        for (const auto& mode : rule_json["modes"]) {
          rule.modes.push_back(mode.get<std::string>());
        }
      }

      if (rule_json.contains("roles") && rule_json["roles"].is_array()) {
        for (const auto& role : rule_json["roles"]) {
          rule.roles.push_back(role.get<std::string>());
        }
      }

      if (rule_json.contains("reason") && rule_json["reason"].is_string()) {
        rule.reason = rule_json["reason"].get<std::string>();
      }

      if (rule_json.contains("constraints") && rule_json["constraints"].is_object()) {
        rule.constraints = rule_json["constraints"];
      }

      projections.push_back(PolicyRuleProjectionDto{
          rule.priority,
          rule.id,
          NormalizePolicyRule(rule),
      });

      rules.push_back(std::move(rule));
    }

    auto digest_res = ComputePolicyDigest(schema_version, default_decision_str,
                                          std::move(projections));
    if (!digest_res) {
      return digest_res.error();
    }

    return std::unique_ptr<PolicyEngine>(new PolicyEngineImpl(
        schema_version, default_decision_str, def_dec_res.value(),
        std::move(rules), digest_res.value()));
  } catch (const std::bad_alloc&) {
    return PolicyInternalError("out of memory while creating policy engine");
  } catch (const ccj::Json::exception&) {
    return PolicySchemaError("policy JSON could not be inspected");
  } catch (...) {
    return PolicyInternalError("unexpected policy engine creation failure");
  }
}

}  // namespace cogito
