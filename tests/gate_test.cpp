// SPDX-License-Identifier: Apache-2.0

#include "cogito/permission_gate.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

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
#include "cogito/tool_schema.hpp"

namespace ccj = cogito::ccj;

namespace {

cogito::Subject MakeSubject(std::string id, std::vector<std::string> roles = {"operator"}) {
  cogito::Subject s;
  s.subject_id = std::move(id);
  s.roles = std::move(roles);
  s.auth_method = "os_user";
  return s;
}

cogito::ToolDescriptor MakeToolDescriptor(std::string name,
                                           cogito::Effect effect = cogito::Effect::None,
                                           bool approval_required = false,
                                           ccj::Json input_schema = ccj::Json::object(),
                                           std::size_t* handler_calls = nullptr) {
  cogito::ToolDescriptor td;
  td.name = std::move(name);
  td.description = "Test tool descriptor";
  if (input_schema.empty()) {
    td.input_schema = ccj::Json{{"type", "object"}, {"additionalProperties", false}};
  } else {
    td.input_schema = std::move(input_schema);
  }
  td.output_schema = nullptr;
  td.effect = effect;
  td.risk = (effect == cogito::Effect::Destructive) ? cogito::Risk::Critical
          : ((effect == cogito::Effect::Write) ? cogito::Risk::Medium : cogito::Risk::Low);
  td.idempotency = (effect == cogito::Effect::Destructive) ? cogito::Idempotency::Unsafe
                 : ((effect == cogito::Effect::Write) ? cogito::Idempotency::Conditional : cogito::Idempotency::Safe);
  td.approval_required = (effect != cogito::Effect::None) ? true : approval_required;
  td.timeout_ms = 3000;
  td.max_output_bytes = 64 * 1024;
  td.provider_id = "test_provider";
  td.invoker_id = "test_invoker";
  td.status = cogito::ToolStatus::Enabled;
  td.SetHandler([handler_calls](const ccj::Json&,
                                const cogito::ToolCallContext&) -> cogito::ToolResult {
    if (handler_calls != nullptr) {
      ++(*handler_calls);
    }
    cogito::ToolResult r;
    r.status = cogito::ToolResultStatus::Ok;
    r.content = ccj::Json::object();
    return r;
  });
  return td;
}

cogito::ToolDescriptor MakeForbiddenToolDescriptor(std::string name, std::string reason) {
  cogito::ToolDescriptor td;
  td.name = std::move(name);
  td.description = "Forbidden tombstone tool";
  td.input_schema = ccj::Json{{"type", "object"}, {"additionalProperties", false}};
  td.output_schema = nullptr;
  td.effect = cogito::Effect::Destructive;
  td.risk = cogito::Risk::Critical;
  td.idempotency = cogito::Idempotency::Unsafe;
  td.approval_required = true;
  td.timeout_ms = 3000;
  td.max_output_bytes = 64 * 1024;
  td.provider_id = "test_provider";
  td.invoker_id = "test_invoker";
  td.status = cogito::ToolStatus::Forbidden;
  td.forbidden_reason = std::move(reason);
  return td;
}

class MockApprovalStore : public cogito::ApprovalStore {
 public:
  const cogito::ApprovalRecord* FindUsable(const cogito::Digest& action_digest,
                                           const cogito::Digest& scope_digest,
                                           const cogito::SessionId& session_id,
                                           cogito::TurnId turn_id,
                                           std::int64_t now_ns) const override {
    for (const auto& rec : records_) {
      if (rec.action_digest == action_digest &&
          rec.scope_digest == scope_digest &&
          rec.session_id == session_id &&
          rec.turn_id == turn_id &&
          !rec.consumed &&
          now_ns < rec.expires_at_ns) {
        return &rec;
      }
    }
    return nullptr;
  }

  cogito::Error AddApproval(cogito::ApprovalRecord record) override {
    records_.push_back(std::move(record));
    return cogito::Error::Ok();
  }

  cogito::Error Consume(const std::string& approval_id, std::int64_t /*now_ns*/) override {
    for (auto& rec : records_) {
      if (rec.approval_id == approval_id) {
        if (rec.consumed) {
          return cogito::Error{cogito::Errc::ApprovalInvalid, cogito::reason::kApprovalAlreadyConsumed, "Already consumed"};
        }
        rec.consumed = true;
        return cogito::Error::Ok();
      }
    }
    return cogito::Error{cogito::Errc::ApprovalInvalid, cogito::reason::kApprovalRequired, "Approval not found"};
  }

 private:
  std::vector<cogito::ApprovalRecord> records_;
};

}  // namespace

TEST_CASE("PermissionGate 8-Stage Pure Evaluation and Permit Issuance", "[gate]") {
  std::size_t handler_calls = 0U;
  cogito::ToolRegistry registry;
  REQUIRE(registry.Register(MakeToolDescriptor("sensor.read", cogito::Effect::None,
                                               false, ccj::Json::object(),
                                               &handler_calls))
              .ok());
  REQUIRE(registry.Register(MakeToolDescriptor("device.set", cogito::Effect::Write, true)).ok());
  REQUIRE(registry.Register(MakeToolDescriptor("system.wipe", cogito::Effect::Destructive, true)).ok());

  // Tool with strict input schema
  ccj::Json strict_schema = {
    {"type", "object"},
    {"properties", {
      {"temperature", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 100.0}}},
      {"mode", {{"type", "string"}, {"enum", ccj::Json::array({"auto", "manual"})}}}
    }},
    {"required", ccj::Json::array({"temperature", "mode"})},
    {"additionalProperties", false}
  };
  REQUIRE(registry.Register(MakeToolDescriptor("thermostat.adjust", cogito::Effect::Write, true, strict_schema)).ok());
  const ccj::Json pattern_schema = {
      {"type", "object"},
      {"properties",
       {{"value",
         {{"type", "string"},
          {"pattern", "^[a-z]*$"},
          {"maxLength", cogito::kMaxMatchStringBytes}}}}},
      {"required", ccj::Json::array({"value"})},
      {"additionalProperties", false}};
  REQUIRE(registry.Register(MakeToolDescriptor(
                               "pattern.match", cogito::Effect::None, false,
                               pattern_schema, &handler_calls))
              .ok());
  REQUIRE(registry.Register(MakeForbiddenToolDescriptor("legacy.command", "Security vulnerability CVE-2026-9999")).ok());
  REQUIRE(registry.Freeze().ok());

  // Policy engine: Allow sensor.* and thermostat.*, Ask for device.set, Deny system.wipe
  const auto policy_json = ccj::Json::parse(R"json({
    "schema_version": 1,
    "default": "deny",
    "rules": [
      {
        "id": "rule-allow-read",
        "priority": 100,
        "tool": "sensor.*",
        "decision": "allow",
        "reason": "Reading sensors is permitted"
      },
      {
        "id": "rule-allow-thermostat",
        "priority": 100,
        "tool": "thermostat.adjust",
        "decision": "allow",
        "reason": "Adjusting thermostat is permitted"
      },
      {
        "id": "rule-ask-device",
        "priority": 100,
        "tool": "device.set",
        "decision": "ask",
        "reason": "Device write requires approval"
      },
      {
        "id": "rule-deny-wipe",
        "priority": 100,
        "tool": "system.wipe",
        "decision": "deny",
        "reason": "Wiping system is strictly forbidden"
      }
    ]
  })json");
  auto policy_res = cogito::PolicyEngine::Create(policy_json);
  REQUIRE(policy_res.ok());
  auto policy = std::move(policy_res).take();

  cogito::TurnBudget budget_spec;
  budget_spec.max_tool_calls = 5;
  budget_spec.timeout_ns = 10'000'000'000LL;  // 10s
  cogito::BudgetTracker budget(budget_spec);
  budget.StartTurn(1'000'000'000LL);  // start at 1s, deadline at 11s

  MockApprovalStore approvals;
  cogito::PermissionGate gate(registry, *policy, budget, &approvals);

  cogito::GateInput base_input;
  base_input.subject = MakeSubject("operator-1");
  base_input.mode = cogito::ExecutionMode::Default;
  base_input.fsm_state = cogito::State::Gate;
  base_input.now_utc = "2026-08-26T12:00:00Z";
  base_input.now_ns = 2'000'000'000LL;  // 2s (within 10s budget)
  base_input.gate_reentry_count = 0;
  base_input.indeterminate_locked = false;

  SECTION("Gate 1: Input Hygiene Failures") {
    // Missing/empty tool name
    cogito::ActionRequest req;
    req.session_id = "sess-01";
    req.turn_id = 1;
    req.action_id = "act-01";
    req.tool_name = "";
    req.arguments = ccj::Json::object();

    cogito::Verdict v = gate.Evaluate(req, base_input);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 1);
    REQUIRE(v.reason_code == cogito::reason::kInputMissingField);
    REQUIRE_FALSE(v.reason.empty());
    REQUIRE(v.rule_id.empty());
    REQUIRE(v.action_digest == base_input.action_digest);
    REQUIRE(v.policy_digest == policy->policy_digest());
    REQUIRE(v.registry_digest == registry.registry_digest());
    REQUIRE(v.evaluated_at_utc == base_input.now_utc);
    REQUIRE(v.expires_at_ns == base_input.now_ns + cogito::kVerdictTtlNs);

    // Tool name too long (> 128 bytes)
    req.tool_name = std::string(129, 'a');
    v = gate.Evaluate(req, base_input);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 1);
    REQUIRE(v.reason_code == cogito::reason::kInputMissingField);

    // Non-UTF8 tool name
    req.tool_name = std::string("sensor\xFF\xFE");
    v = gate.Evaluate(req, base_input);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 1);
    REQUIRE(v.reason_code == cogito::reason::kInputNotUtf8);

    // Invalid UTF-8 inside arguments is rejected before registry lookup.
    req.tool_name = "sensor.read";
    req.arguments = std::string(1U, static_cast<char>(0xC3));
    v = gate.Evaluate(req, base_input);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 1);
    REQUIRE(v.reason_code == cogito::reason::kInputNotUtf8);

    // Arguments exceeding max byte size
    req.tool_name = "sensor.read";
    req.arguments = ccj::Json{{"payload", std::string(70000, 'x')}};
    v = gate.Evaluate(req, base_input);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 1);
    REQUIRE(v.reason_code == cogito::reason::kInputTooLarge);

    // Arguments exceeding max depth
    ccj::Json deep_obj = ccj::Json::object();
    for (int i = 0; i < 20; ++i) {
      ccj::Json nested = ccj::Json::object();
      nested["inner"] = deep_obj;
      deep_obj = nested;
    }
    req.arguments = deep_obj;
    v = gate.Evaluate(req, base_input);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 1);
    REQUIRE(v.reason_code == cogito::reason::kInputDepthExceeded);
  }

  SECTION("Gate 1: Size and Depth Boundaries Are Inclusive") {
    cogito::ActionRequest req;
    req.session_id = "sess-boundary";
    req.turn_id = 1;
    req.action_id = "act-boundary";
    req.tool_name = "sensor.read";
    req.arguments = ccj::Json{{"payload", "x"}};

    const auto serialized = ccj::Serialize(req.arguments);
    REQUIRE(serialized.ok());
    cogito::GateLimits limits;
    limits.max_action_bytes = serialized.value().size();
    limits.max_action_depth = 16;
    cogito::PermissionGate exact_size_gate(registry, *policy, budget, &approvals,
                                           limits);
    REQUIRE(exact_size_gate.Evaluate(req, base_input).gate_stage != 1);

    limits.max_action_bytes = serialized.value().size() - 1U;
    cogito::PermissionGate over_size_gate(registry, *policy, budget, &approvals,
                                          limits);
    const auto too_large = over_size_gate.Evaluate(req, base_input);
    REQUIRE(too_large.gate_stage == 1);
    REQUIRE(too_large.reason_code == cogito::reason::kInputTooLarge);

    req.arguments = ccj::Json::object();
    limits.max_action_bytes = 1024U;
    limits.max_action_depth = 1;
    cogito::PermissionGate exact_depth_gate(registry, *policy, budget, &approvals,
                                            limits);
    REQUIRE(exact_depth_gate.Evaluate(req, base_input).gate_stage == 7);

    limits.max_action_depth = 0;
    cogito::PermissionGate over_depth_gate(registry, *policy, budget, &approvals,
                                           limits);
    const auto too_deep = over_depth_gate.Evaluate(req, base_input);
    REQUIRE(too_deep.gate_stage == 1);
    REQUIRE(too_deep.reason_code == cogito::reason::kInputDepthExceeded);
  }

  SECTION("Gate 2: Tool Registration Status (Tombstone vs Absent)") {
    // Absent tool
    cogito::ActionRequest req;
    req.session_id = "sess-01";
    req.turn_id = 1;
    req.action_id = "act-01";
    req.tool_name = "unknown.tool";
    req.arguments = ccj::Json::object();

    cogito::Verdict v = gate.Evaluate(req, base_input);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 2);
    REQUIRE(v.reason_code == cogito::reason::kToolNotRegistered);

    // Forbidden / Tombstone tool
    req.tool_name = "legacy.command";
    v = gate.Evaluate(req, base_input);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 2);
    REQUIRE(v.reason_code == cogito::reason::kToolForbidden);
    REQUIRE(v.reason == "Security vulnerability CVE-2026-9999");
  }

  SECTION("Gate 3: Argument Schema Validation") {
    // Schema violation
    cogito::ActionRequest req;
    req.session_id = "sess-01";
    req.turn_id = 1;
    req.action_id = "act-01";
    req.tool_name = "thermostat.adjust";
    req.arguments = ccj::Json{{"temperature", 150.0}, {"mode", "auto"}};  // maximum is 100.0

    cogito::Verdict v = gate.Evaluate(req, base_input);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 3);
    REQUIRE(v.reason_code == cogito::reason::kSchemaViolation);

    // Missing required field
    req.arguments = ccj::Json{{"temperature", 25.0}};  // missing "mode"
    v = gate.Evaluate(req, base_input);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 3);
    REQUIRE(v.reason_code == cogito::reason::kSchemaViolation);

    cogito::GateLimits pattern_limits;
    pattern_limits.max_action_bytes = 100'000U;
    cogito::PermissionGate pattern_gate(registry, *policy, budget, &approvals,
                                        pattern_limits);
    req.tool_name = "pattern.match";
    req.arguments = ccj::Json{{"value", std::string(cogito::kMaxMatchStringBytes,
                                                     'a')}};
    v = pattern_gate.Evaluate(req, base_input);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 3);
    REQUIRE(v.reason_code == cogito::reason::kPatternTimeout);
    REQUIRE(handler_calls == 0U);
  }

  SECTION("Gate 4: FSM State Validation") {
    cogito::ActionRequest req;
    req.session_id = "sess-01";
    req.turn_id = 1;
    req.action_id = "act-01";
    req.tool_name = "sensor.read";
    req.arguments = ccj::Json::object();

    cogito::GateInput in = base_input;
    in.fsm_state = cogito::State::Infer;

    cogito::Verdict v = gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 4);
    REQUIRE(v.reason_code == cogito::reason::kInvalidFsmState);

    in.fsm_state = cogito::State::Execute;
    v = gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 4);
    REQUIRE(v.reason_code == cogito::reason::kInvalidFsmState);

    in.fsm_state = cogito::State::Idle;
    v = gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 4);
    REQUIRE(v.reason_code == cogito::reason::kInvalidFsmState);
  }

  SECTION("Gate 5: Mode Ceiling, Policy Denials, and Indeterminate Lockdown") {
    cogito::ActionRequest req;
    req.session_id = "sess-01";
    req.turn_id = 1;
    req.action_id = "act-01";
    req.tool_name = "thermostat.adjust";
    req.arguments = ccj::Json{{"temperature", 25.0}, {"mode", "auto"}};

    // Mode ceiling violation (ReadOnly mode cannot execute Write tool)
    cogito::GateInput in = base_input;
    in.mode = cogito::ExecutionMode::ReadOnly;

    cogito::Verdict v = gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 5);
    REQUIRE(v.reason_code == cogito::reason::kModeDenied);

    // Explicit Policy Deny rule
    req.tool_name = "system.wipe";
    req.arguments = ccj::Json::object();
    in.mode = cogito::ExecutionMode::Default;

    v = gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 5);
    REQUIRE(v.reason_code == cogito::reason::kPolicyDenied);
    REQUIRE(v.rule_id == "rule-deny-wipe");

    // Indeterminate lockdown overrides Policy Allow
    req.tool_name = "sensor.read";
    req.arguments = ccj::Json::object();
    in.indeterminate_locked = true;

    v = gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 5);
    REQUIRE(v.reason_code == cogito::reason::kIndeterminateLockdown);

    req.tool_name = "sensor.read";
    in = base_input;
    in.subject.subject_id.clear();
    v = gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 5);
    REQUIRE(v.reason_code == cogito::reason::kRoleDenied);
  }

  SECTION("Gate 6: Deadline and Resource Limits") {
    cogito::ActionRequest req;
    req.session_id = "sess-01";
    req.turn_id = 1;
    req.action_id = "act-01";
    req.tool_name = "sensor.read";
    req.arguments = ccj::Json::object();

    // The exact deadline is expired (start 1s + timeout 10s = 11s).
    cogito::GateInput in = base_input;
    in.now_ns = 11'000'000'000LL;

    cogito::Verdict v = gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 6);
    REQUIRE(v.reason_code == cogito::reason::kBudgetDeadline);

    in.now_ns = 10'999'999'999LL;
    v = gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Allow);

    // Tool call limit exceeded
    in.now_ns = 2'000'000'000LL;
    for (std::size_t i = 0; i < 5; ++i) {
      REQUIRE(budget.RecordToolCall(100).ok());
    }
    v = gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 6);
    REQUIRE(v.reason_code == cogito::reason::kBudgetToolCalls);
  }

  SECTION("Gate 7: Approval Requirements, Reentry Threshold, and Allow") {
    cogito::ActionRequest req;
    req.session_id = "sess-01";
    req.turn_id = 1;
    req.action_id = "act-01";
    req.tool_name = "sensor.read";
    req.arguments = ccj::Json::object();

    // 1. Safe tool with Policy Allow -> Allow
    cogito::Verdict v = gate.Evaluate(req, base_input);
    REQUIRE(v.decision == cogito::Decision::Allow);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kAllowed);
    REQUIRE(v.rule_id == "rule-allow-read");
    REQUIRE(v.expires_at_ns == base_input.now_ns + cogito::kVerdictTtlNs);

    // 2. Tool requiring approval without record -> Ask
    req.tool_name = "device.set";
    v = gate.Evaluate(req, base_input);
    REQUIRE(v.decision == cogito::Decision::Ask);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kApprovalRequired);

    // 3. Reentry threshold exceeded (gate_reentry_count = 1 >= max_reentry 1) -> Deny (2nd Ask blocked)
    cogito::GateInput reentry_in = base_input;
    reentry_in.gate_reentry_count = 1;
    v = gate.Evaluate(req, reentry_in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kApprovalReentryExceeded);

    // 4. Valid usable approval present -> Allow
    auto action_digest_res = cogito::ComputeActionDigest(
        req.session_id, req.turn_id, req.action_id, req.tool_name, req.arguments);
    REQUIRE(action_digest_res.ok());

    auto scope_digest_res = cogito::ComputePermitDigest(
        action_digest_res.value(), base_input.subject.subject_id,
        static_cast<std::uint64_t>(base_input.mode), policy->policy_digest(), registry.registry_digest());
    REQUIRE(scope_digest_res.ok());

    base_input.action_digest = action_digest_res.value();
    base_input.permit_scope_digest = scope_digest_res.value();

    cogito::ApprovalRecord approval_rec;
    approval_rec.approval_id = "appr-1234";
    approval_rec.action_digest = base_input.action_digest;
    approval_rec.scope_digest = base_input.permit_scope_digest;
    approval_rec.session_id = req.session_id;
    approval_rec.turn_id = req.turn_id;
    approval_rec.requester = base_input.subject;
    approval_rec.approver = MakeSubject("sec-officer-1");
    approval_rec.granted_at_utc = "2026-08-26T12:00:01Z";
    approval_rec.expires_at_ns = base_input.now_ns + 5'000'000'000LL;  // 5s TTL
    approval_rec.nonce = "nonce-xyz";
    approval_rec.consumed = false;
    REQUIRE(approvals.AddApproval(approval_rec).ok());

    v = gate.Evaluate(req, base_input);
    REQUIRE(v.decision == cogito::Decision::Allow);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kAllowed);
    REQUIRE(v.rule_id == "approval:appr-1234");
  }

  SECTION("Compound Multi-Defect Priority Ordering") {
    // Defect at Gate 1 (empty name) and Gate 2 (forbidden tool) -> Gate 1 wins
    cogito::ActionRequest req;
    req.tool_name = "";
    req.arguments = ccj::Json::object();
    cogito::Verdict v = gate.Evaluate(req, base_input);
    REQUIRE(v.gate_stage == 1);

    // Defect at Gate 2 (absent) and Gate 3 (schema violation) -> Gate 2 wins
    req.tool_name = "nonexistent.tool";
    req.arguments = ccj::Json{{"bad", 123}};
    v = gate.Evaluate(req, base_input);
    REQUIRE(v.gate_stage == 2);

    // Defect at Gate 3 (schema violation) and Gate 4 (FSM state) -> Gate 3 wins
    req.tool_name = "thermostat.adjust";
    req.arguments = ccj::Json{{"temperature", 999.0}};  // schema violation
    cogito::GateInput in = base_input;
    in.fsm_state = cogito::State::Infer;  // wrong state
    v = gate.Evaluate(req, in);
    REQUIRE(v.gate_stage == 3);

    // Defect at Gate 4 (FSM state) and Gate 5 (mode ceiling) -> Gate 4 wins
    req.arguments = ccj::Json{{"temperature", 25.0}, {"mode", "auto"}};
    in.mode = cogito::ExecutionMode::ReadOnly;  // mode ceiling
    v = gate.Evaluate(req, in);
    REQUIRE(v.gate_stage == 4);

    // Defect at Gate 5 (mode ceiling) and Gate 6 (deadline) -> Gate 5 wins
    in.fsm_state = cogito::State::Gate;
    in.now_ns = 20'000'000'000LL;  // deadline exceeded
    v = gate.Evaluate(req, in);
    REQUIRE(v.gate_stage == 5);
  }

  SECTION("Pure Evaluation Invariance: Side-Effect Free") {
    cogito::ActionRequest req;
    req.session_id = "sess-pure";
    req.turn_id = 1;
    req.action_id = "act-pure";
    req.tool_name = "sensor.read";
    req.arguments = ccj::Json::object();

    const std::size_t initial_tools_used = budget.tool_calls_count();

    // Call Evaluate 10 times in a row
    for (int i = 0; i < 10; ++i) {
      cogito::Verdict v = gate.Evaluate(req, base_input);
      REQUIRE(v.decision == cogito::Decision::Allow);
      REQUIRE(v.gate_stage == 7);
      REQUIRE(v.reason_code == cogito::reason::kAllowed);
    }

    // Budget state completely untouched
    REQUIRE(budget.tool_calls_count() == initial_tools_used);
    REQUIRE(handler_calls == 0U);
  }

  SECTION("IssuePermit on Verdict::Allow") {
    const cogito::LookupResult lk = registry.Lookup("sensor.read");
    REQUIRE(lk.kind == cogito::LookupKind::Enabled);
    REQUIRE(lk.desc != nullptr);

    cogito::Digest act_dig;
    act_dig.bytes.fill(0x33);
    cogito::Digest scope_dig;
    scope_dig.bytes.fill(0x44);

    const std::int64_t now_ns = 5'000'000'000LL;
    cogito::ExecutionPermit permit = gate.IssuePermit(*lk.desc, act_dig, scope_dig, now_ns);

    REQUIRE(permit.valid());
    REQUIRE(permit.tool_name() == "sensor.read");
    REQUIRE(permit.action_digest() == act_dig);
    REQUIRE(permit.permit_scope_digest() == scope_dig);
    REQUIRE(permit.idempotency_key() == act_dig.hex());
    REQUIRE(permit.timeout_ms() == 3000);
    REQUIRE(permit.effect() == cogito::Effect::None);
    REQUIRE(permit.expires_ns() == now_ns + 3000 * 1'000'000LL);

    // CheckUsable verification
    REQUIRE(permit.CheckUsable("sensor.read", scope_dig, now_ns + 1000).ok());
    REQUIRE_FALSE(permit.CheckUsable("other.tool", scope_dig, now_ns + 1000).ok());

    cogito::Digest wrong_scope;
    wrong_scope.bytes.fill(0x99);
    REQUIRE_FALSE(permit.CheckUsable("sensor.read", wrong_scope, now_ns + 1000).ok());

    // Expired check
    REQUIRE_FALSE(permit.CheckUsable("sensor.read", scope_dig, permit.expires_ns()).ok());

    const std::int64_t near_max =
        std::numeric_limits<std::int64_t>::max() - 1;
    auto saturated = gate.IssuePermit(*lk.desc, act_dig, scope_dig, near_max);
    REQUIRE(saturated.expires_ns() ==
            std::numeric_limits<std::int64_t>::max());
    REQUIRE(saturated.CheckUsable("sensor.read", scope_dig, near_max).ok());
    REQUIRE_FALSE(saturated.CheckUsable(
                      "sensor.read", scope_dig,
                      std::numeric_limits<std::int64_t>::max())
                      .ok());
  }

  SECTION("Verdict expiration saturates without signed overflow") {
    cogito::ActionRequest req;
    req.session_id = "sess-overflow";
    req.turn_id = 1;
    req.action_id = "act-overflow";
    req.tool_name = "sensor.read";
    req.arguments = ccj::Json::object();

    auto in = base_input;
    in.now_ns = std::numeric_limits<std::int64_t>::max() - 1;
    const auto verdict = gate.Evaluate(req, in);
    REQUIRE(verdict.expires_at_ns ==
            std::numeric_limits<std::int64_t>::max());
    REQUIRE(verdict.gate_stage == 6);
    REQUIRE(verdict.reason_code == cogito::reason::kBudgetDeadline);
    REQUIRE(handler_calls == 0U);
  }

  SECTION("Gate 7 Defense-in-Depth Store Record Validation") {
    struct PassthroughStore : public cogito::ApprovalStore {
      const cogito::ApprovalRecord* return_record = nullptr;
      const cogito::ApprovalRecord* FindUsable(
          const cogito::Digest&, const cogito::Digest&,
          const cogito::SessionId&, cogito::TurnId,
          std::int64_t) const override {
        return return_record;
      }
      cogito::Error AddApproval(cogito::ApprovalRecord) override { return cogito::Error::Ok(); }
      cogito::Error Consume(const std::string&, std::int64_t) override { return cogito::Error::Ok(); }
    };

    PassthroughStore passthrough;
    cogito::PermissionGate pass_gate(registry, *policy, budget, &passthrough);

    cogito::ActionRequest req;
    req.session_id = "sess-def";
    req.turn_id = 1;
    req.action_id = "act-def";
    req.tool_name = "device.set";
    req.arguments = ccj::Json::object();

    auto action_digest_res = cogito::ComputeActionDigest(
        req.session_id, req.turn_id, req.action_id, req.tool_name, req.arguments);
    REQUIRE(action_digest_res.ok());
    auto scope_digest_res = cogito::ComputePermitDigest(
        action_digest_res.value(), base_input.subject.subject_id,
        static_cast<std::uint64_t>(base_input.mode), policy->policy_digest(), registry.registry_digest());
    REQUIRE(scope_digest_res.ok());

    cogito::GateInput in = base_input;
    in.action_digest = action_digest_res.value();
    in.permit_scope_digest = scope_digest_res.value();

    cogito::ApprovalRecord rec;
    rec.approval_id = "appr-valid";
    rec.action_digest = in.action_digest;
    rec.scope_digest = in.permit_scope_digest;
    rec.session_id = req.session_id;
    rec.turn_id = req.turn_id;
    rec.requester = in.subject;
    rec.approver = MakeSubject("sec-officer");
    rec.granted_at_utc = "2026-08-26T12:00:00Z";
    rec.expires_at_ns = in.now_ns + 10'000'000'000LL;
    rec.nonce = "nonce-1";
    rec.consumed = false;

    // 1. Expired record returned by store
    rec.expires_at_ns = in.now_ns; // expired at exact boundary
    passthrough.return_record = &rec;
    auto v = pass_gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kApprovalExpired);

    // 2. Consumed record returned by store
    rec.expires_at_ns = in.now_ns + 10'000'000'000LL;
    rec.consumed = true;
    v = pass_gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kApprovalAlreadyConsumed);

    // 3. Action digest mismatch
    rec.consumed = false;
    rec.action_digest.bytes.fill(0xEE);
    v = pass_gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kApprovalScopeMismatch);

    // 4. Scope digest mismatch
    rec.action_digest = in.action_digest;
    rec.scope_digest.bytes.fill(0xEE);
    v = pass_gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kApprovalScopeMismatch);

    // 5. Session ID mismatch
    rec.scope_digest = in.permit_scope_digest;
    rec.session_id = "other-sess";
    v = pass_gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kApprovalScopeMismatch);

    // 6. Turn ID mismatch
    rec.session_id = req.session_id;
    rec.turn_id = 99;
    v = pass_gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kApprovalScopeMismatch);

    // 7. Empty approval_id
    rec.turn_id = req.turn_id;
    rec.approval_id = "";
    v = pass_gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kApprovalScopeMismatch);
  }

  SECTION("Gate 7 Reentry Limits and Null ApprovalStore Boundaries") {
    cogito::PermissionGate null_store_gate(registry, *policy, budget, nullptr);

    cogito::ActionRequest req;
    req.session_id = "sess-reentry";
    req.turn_id = 1;
    req.action_id = "act-reentry";
    req.tool_name = "device.set";
    req.arguments = ccj::Json::object();

    // Reentry count 0 with null store -> Ask
    cogito::GateInput in = base_input;
    in.gate_reentry_count = 0;
    auto v = null_store_gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Ask);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kApprovalRequired);

    // Reentry count 1 with null store (max_reentry = 1) -> Deny
    in.gate_reentry_count = 1;
    v = null_store_gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kApprovalReentryExceeded);

    // Negative reentry count -> Deny
    in.gate_reentry_count = -1;
    v = null_store_gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kApprovalReentryExceeded);

    // Configured max_reentry = 0 -> Denies immediately on 0
    cogito::GateLimits zero_limit;
    zero_limit.max_reentry = 0;
    cogito::PermissionGate zero_gate(registry, *policy, budget, nullptr, zero_limit);
    in.gate_reentry_count = 0;
    v = zero_gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kApprovalReentryExceeded);

    // Configured max_reentry = 3
    cogito::GateLimits custom_limit;
    custom_limit.max_reentry = 3;
    cogito::PermissionGate multi_gate(registry, *policy, budget, nullptr, custom_limit);
    for (int r = 0; r < 3; ++r) {
      in.gate_reentry_count = r;
      v = multi_gate.Evaluate(req, in);
      REQUIRE(v.decision == cogito::Decision::Ask);
      REQUIRE(v.gate_stage == 7);
      REQUIRE(v.reason_code == cogito::reason::kApprovalRequired);
    }
    in.gate_reentry_count = 3;
    v = multi_gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 7);
    REQUIRE(v.reason_code == cogito::reason::kApprovalReentryExceeded);
  }

  SECTION("IssuePermit Boundary Conditions and Move-Only Semantics") {
    cogito::Digest act_dig;
    act_dig.bytes.fill(0x11);
    cogito::Digest scope_dig;
    scope_dig.bytes.fill(0x22);
    const std::int64_t now = 1'000'000'000LL;

    // 1. ToolDescriptor with Forbidden status
    auto forbidden_td = MakeForbiddenToolDescriptor("bad.tool", "Vulnerability");
    auto p1 = gate.IssuePermit(forbidden_td, act_dig, scope_dig, now);
    REQUIRE(p1.expires_ns() == now);
    REQUIRE(p1.IsExpired(now));

    // 2. ToolDescriptor with zero timeout
    auto td_zero_timeout = MakeToolDescriptor("zero.timeout");
    td_zero_timeout.timeout_ms = 0;
    auto p2 = gate.IssuePermit(td_zero_timeout, act_dig, scope_dig, now);
    REQUIRE(p2.expires_ns() == now);
    REQUIRE(p2.IsExpired(now));

    // 3. ToolDescriptor with negative timeout
    auto td_neg_timeout = MakeToolDescriptor("neg.timeout");
    td_neg_timeout.timeout_ms = -100;
    auto p3 = gate.IssuePermit(td_neg_timeout, act_dig, scope_dig, now);
    REQUIRE(p3.expires_ns() == now);
    REQUIRE(p3.IsExpired(now));

    // 4. ToolDescriptor with excessive timeout (> 3'600'000 ms)
    auto td_huge_timeout = MakeToolDescriptor("huge.timeout");
    td_huge_timeout.timeout_ms = 4'000'000;
    auto p4 = gate.IssuePermit(td_huge_timeout, act_dig, scope_dig, now);
    REQUIRE(p4.expires_ns() == now);
    REQUIRE(p4.IsExpired(now));

    // 5. ToolDescriptor with empty name
    auto td_empty_name = MakeToolDescriptor("");
    auto p5 = gate.IssuePermit(td_empty_name, act_dig, scope_dig, now);
    REQUIRE_FALSE(p5.valid());
    REQUIRE(p5.expires_ns() == now);

    // 6. Move invalidates source permit
    auto valid_td = MakeToolDescriptor("valid.tool");
    valid_td.timeout_ms = 5000;
    auto valid_permit = gate.IssuePermit(valid_td, act_dig, scope_dig, now);
    REQUIRE(valid_permit.valid());
    REQUIRE(valid_permit.tool_name() == "valid.tool");

    cogito::ExecutionPermit target = std::move(valid_permit);
    REQUIRE(target.valid());
    REQUIRE(target.tool_name() == "valid.tool");
    REQUIRE_FALSE(valid_permit.valid());
    REQUIRE(valid_permit.tool_name().empty());
    REQUIRE(valid_permit.CheckUsable("valid.tool", scope_dig, now + 100).code == cogito::Errc::Internal);
  }

  SECTION("Defect Precedence: Gate 6 Deny beats Gate 7 Ask") {
    // Action requires approval (Gate 7 would normally return Ask)
    cogito::ActionRequest req;
    req.session_id = "sess-prio";
    req.turn_id = 1;
    req.action_id = "act-prio";
    req.tool_name = "device.set";
    req.arguments = ccj::Json::object();

    // But turn deadline is exceeded (Gate 6 defect)
    cogito::GateInput in = base_input;
    in.now_ns = 15'000'000'000LL;  // deadline at 11s

    auto v = gate.Evaluate(req, in);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.gate_stage == 6);
    REQUIRE(v.reason_code == cogito::reason::kBudgetDeadline);
  }
}

