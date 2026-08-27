// SPDX-License-Identifier: Apache-2.0

#include "cogito/policy.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "cogito/canonical_json.hpp"

namespace ccj = cogito::ccj;

namespace {

cogito::Subject MakeSubject(std::string id, std::vector<std::string> roles = {}) {
  cogito::Subject s;
  s.subject_id = std::move(id);
  s.roles = std::move(roles);
  s.auth_method = "os_user";
  return s;
}

}  // namespace

TEST_CASE("PolicyEngine validates schema and rejects invalid JSON", "[policy][schema]") {
  SECTION("Valid minimal policy") {
    const auto j = ccj::Json::parse(R"json({
      "schema_version": 1,
      "default": "deny",
      "rules": []
    })json");
    auto engine_res = cogito::PolicyEngine::Create(j);
    REQUIRE(engine_res.ok());
    auto engine = std::move(engine_res).take();
    REQUIRE(engine->schema_version() == 1U);
    REQUIRE(engine->default_decision() == "deny");
    REQUIRE_FALSE(engine->policy_digest().is_zero());
  }

  SECTION("Rejects invalid schema version") {
    const auto j = ccj::Json::parse(R"json({
      "schema_version": 2,
      "default": "deny",
      "rules": []
    })json");
    auto res = cogito::PolicyEngine::Create(j);
    REQUIRE_FALSE(res.ok());
    REQUIRE(res.error().code == cogito::Errc::SchemaViolation);
  }

  SECTION("Rejects missing required properties") {
    const auto j = ccj::Json::parse(R"json({
      "schema_version": 1,
      "rules": []
    })json");
    auto res = cogito::PolicyEngine::Create(j);
    REQUIRE_FALSE(res.ok());
    REQUIRE(res.error().code == cogito::Errc::SchemaViolation);
  }

  SECTION("Rejects duplicate rule IDs") {
    const auto j = ccj::Json::parse(R"json({
      "schema_version": 1,
      "default": "deny",
      "rules": [
        {
          "id": "rule-1",
          "priority": 10,
          "tool": "*",
          "decision": "allow"
        },
        {
          "id": "rule-1",
          "priority": 20,
          "tool": "calc.*",
          "decision": "deny"
        }
      ]
    })json");
    auto res = cogito::PolicyEngine::Create(j);
    REQUIRE_FALSE(res.ok());
    REQUIRE(res.error().code == cogito::Errc::DuplicateKey);
  }

  SECTION("Rejects effect_min > effect_max") {
    const auto j = ccj::Json::parse(R"json({
      "schema_version": 1,
      "default": "deny",
      "rules": [
        {
          "id": "rule-bad-effects",
          "priority": 10,
          "tool": "*",
          "effect_min": "destructive",
          "effect_max": "write",
          "decision": "allow"
        }
      ]
    })json");
    auto res = cogito::PolicyEngine::Create(j);
    REQUIRE_FALSE(res.ok());
    REQUIRE(res.error().code == cogito::Errc::SchemaViolation);
  }

  SECTION("Rejects unknown properties and malformed field types") {
    const std::vector<std::string> invalid_documents{
        R"json([])json",
        R"json({"schema_version":1,"default":"deny","rules":[],"extra":1})json",
        R"json({"schema_version":1,"default":"deny","rules":{}})json",
        R"json({"schema_version":1,"default":"permit","rules":[]})json",
        R"json({"schema_version":1,"default":"deny","rules":[{"id":"x","priority":-1,"tool":"*","decision":"deny"}]})json",
        R"json({"schema_version":1,"default":"deny","rules":[{"id":"x","priority":100001,"tool":"*","decision":"deny"}]})json",
        R"json({"schema_version":1,"default":"deny","rules":[{"id":"x","priority":1,"tool":"*","decision":"deny","constraints":null}]})json",
    };
    for (const std::string& document : invalid_documents) {
      CAPTURE(document);
      const auto result = cogito::PolicyEngine::Create(ccj::Json::parse(document));
      REQUIRE_FALSE(result.ok());
      REQUIRE(result.error().code == cogito::Errc::SchemaViolation);
      REQUIRE(result.error().reason_code == cogito::reason::kSchemaViolation);
    }
  }

  SECTION("Rejects malformed wildcard syntax") {
    for (const std::string pattern : {"motor*", "motor.*.start", ".*", "**"}) {
      CAPTURE(pattern);
      const ccj::Json document{
          {"schema_version", 1},
          {"default", "deny"},
          {"rules", ccj::Json::array({ccj::Json{
                        {"id", "bad-pattern"},
                        {"priority", 1},
                        {"tool", pattern},
                        {"decision", "deny"},
                    }})},
      };
      const auto result = cogito::PolicyEngine::Create(document);
      REQUIRE_FALSE(result.ok());
      REQUIRE(result.error().code == cogito::Errc::SchemaViolation);
    }
  }
}

TEST_CASE("PolicyEngine evaluates tool patterns and effect limits", "[policy][evaluate][matching]") {
  const auto j = ccj::Json::parse(R"json({
    "schema_version": 1,
    "default": "deny",
    "rules": [
      {
        "id": "rule-wildcard",
        "priority": 10,
        "tool": "*",
        "modes": ["*"],
        "decision": "ask",
        "reason": "Default ask for all tools"
      },
      {
        "id": "rule-sensor",
        "priority": 20,
        "tool": "sensor.*",
        "effect_max": "none",
        "decision": "allow",
        "reason": "Allow readonly sensors"
      },
      {
        "id": "rule-motor-exact",
        "priority": 30,
        "tool": "motor.start",
        "decision": "allow",
        "reason": "Allow motor start directly"
      },
      {
        "id": "rule-editor-write",
        "priority": 25,
        "tool": "editor.*",
        "effect_min": "write",
        "effect_max": "write",
        "modes": ["default"],
        "decision": "allow",
        "reason": "Allow writes only in default mode"
      }
    ]
  })json");

  auto engine = cogito::PolicyEngine::Create(j).take();
  const auto subject = MakeSubject("operator-1", {"operator"});

  SECTION("Prefix match sensor.* allows readonly query") {
    auto v = engine->Evaluate("sensor.temperature", cogito::Effect::None, ccj::Json::object(),
                              subject, cogito::ExecutionMode::Default);
    REQUIRE(v.decision == cogito::Decision::Allow);
    REQUIRE(v.rule_id == "rule-sensor");
    REQUIRE(v.reason_code == cogito::reason::kAllowed);
    REQUIRE(v.reason == "Allow readonly sensors");
  }

  SECTION("Sensor rule effect_max none prevents higher effect from matching it") {
    // If sensor tool has Write effect, rule-sensor effect_max:none does not match, falls back to rule-wildcard
    auto v = engine->Evaluate("sensor.calibrate", cogito::Effect::Write, ccj::Json::object(),
                              subject, cogito::ExecutionMode::Default);
    REQUIRE(v.decision == cogito::Decision::Ask);
    REQUIRE(v.rule_id == "rule-wildcard");
    REQUIRE(v.reason_code == cogito::reason::kApprovalRequired);
  }

  SECTION("Exact match motor.start takes higher priority") {
    auto v = engine->Evaluate("motor.start", cogito::Effect::Write, ccj::Json::object(),
                              subject, cogito::ExecutionMode::Default);
    REQUIRE(v.decision == cogito::Decision::Allow);
    REQUIRE(v.rule_id == "rule-motor-exact");
  }

  SECTION("Unmatched prefix falls back to wildcard") {
    auto v = engine->Evaluate("pump.start", cogito::Effect::Write, ccj::Json::object(),
                              subject, cogito::ExecutionMode::Default);
    REQUIRE(v.decision == cogito::Decision::Ask);
    REQUIRE(v.rule_id == "rule-wildcard");
  }

  SECTION("Mode and effect bounds must all match") {
    const auto matching = engine->Evaluate(
        "editor.save", cogito::Effect::Write, ccj::Json::object(), subject,
        cogito::ExecutionMode::Default);
    REQUIRE(matching.decision == cogito::Decision::Allow);
    REQUIRE(matching.rule_id == "rule-editor-write");

    const auto below_min = engine->Evaluate(
        "editor.save", cogito::Effect::None, ccj::Json::object(), subject,
        cogito::ExecutionMode::Default);
    REQUIRE(below_min.decision == cogito::Decision::Ask);
    REQUIRE(below_min.rule_id == "rule-wildcard");

    const auto above_max = engine->Evaluate(
        "editor.save", cogito::Effect::Destructive, ccj::Json::object(), subject,
        cogito::ExecutionMode::Default);
    REQUIRE(above_max.decision == cogito::Decision::Ask);
    REQUIRE(above_max.rule_id == "rule-wildcard");

    const auto wrong_mode = engine->Evaluate(
        "editor.save", cogito::Effect::Write, ccj::Json::object(), subject,
        cogito::ExecutionMode::Edit);
    REQUIRE(wrong_mode.decision == cogito::Decision::Ask);
    REQUIRE(wrong_mode.rule_id == "rule-wildcard");
  }

  SECTION("Dotted prefix does not match its base or a sibling prefix") {
    const auto base = engine->Evaluate("sensor", cogito::Effect::None,
                                       ccj::Json::object(), subject,
                                       cogito::ExecutionMode::Default);
    REQUIRE(base.decision == cogito::Decision::Ask);
    REQUIRE(base.rule_id == "rule-wildcard");

    const auto sibling = engine->Evaluate("sensors.temperature", cogito::Effect::None,
                                          ccj::Json::object(), subject,
                                          cogito::ExecutionMode::Default);
    REQUIRE(sibling.decision == cogito::Decision::Ask);
    REQUIRE(sibling.rule_id == "rule-wildcard");
  }
}

TEST_CASE("PolicyEngine enforces G0-29 mode upper bounds fail closed", "[policy][evaluate][mode_ceiling]") {
  const auto j = ccj::Json::parse(R"json({
    "schema_version": 1,
    "default": "allow",
    "rules": [
      {
        "id": "rule-allow-all",
        "priority": 100,
        "tool": "*",
        "decision": "allow",
        "reason": "Allow everything"
      }
    ]
  })json");

  auto engine = cogito::PolicyEngine::Create(j).take();
  const auto subject = MakeSubject("admin-1", {"admin"});

  // ReadOnly mode only allows Effect::None
  auto v_ro_write = engine->Evaluate("db.update", cogito::Effect::Write, ccj::Json::object(),
                                     subject, cogito::ExecutionMode::ReadOnly);
  REQUIRE(v_ro_write.decision == cogito::Decision::Deny);
  REQUIRE(v_ro_write.gate_stage == 5);
  REQUIRE(v_ro_write.reason_code == cogito::reason::kModeDenied);

  auto v_ro_destr = engine->Evaluate("fs.wipe", cogito::Effect::Destructive, ccj::Json::object(),
                                     subject, cogito::ExecutionMode::ReadOnly);
  REQUIRE(v_ro_destr.decision == cogito::Decision::Deny);
  REQUIRE(v_ro_destr.gate_stage == 5);
  REQUIRE(v_ro_destr.reason_code == cogito::reason::kModeDenied);

  // Plan mode has same ceiling as ReadOnly (Effect::None)
  auto v_plan_write = engine->Evaluate("db.update", cogito::Effect::Write, ccj::Json::object(),
                                       subject, cogito::ExecutionMode::Plan);
  REQUIRE(v_plan_write.decision == cogito::Decision::Deny);
  REQUIRE(v_plan_write.reason_code == cogito::reason::kModeDenied);

  // Edit mode allows Effect::None and Effect::Write, but denies Effect::Destructive
  auto v_edit_write = engine->Evaluate("db.update", cogito::Effect::Write, ccj::Json::object(),
                                       subject, cogito::ExecutionMode::Edit);
  REQUIRE(v_edit_write.decision == cogito::Decision::Allow);

  auto v_edit_destr = engine->Evaluate("fs.wipe", cogito::Effect::Destructive, ccj::Json::object(),
                                       subject, cogito::ExecutionMode::Edit);
  REQUIRE(v_edit_destr.decision == cogito::Decision::Deny);
  REQUIRE(v_edit_destr.gate_stage == 5);
  REQUIRE(v_edit_destr.reason_code == cogito::reason::kModeDenied);

  const auto invalid_mode = engine->Evaluate(
      "sensor.read", cogito::Effect::None, ccj::Json::object(), subject,
      static_cast<cogito::ExecutionMode>(255U));
  REQUIRE(invalid_mode.decision == cogito::Decision::Deny);
  REQUIRE(invalid_mode.reason_code == cogito::reason::kModeDenied);

  const auto invalid_effect = engine->Evaluate(
      "sensor.read", static_cast<cogito::Effect>(255U), ccj::Json::object(),
      subject, cogito::ExecutionMode::Default);
  REQUIRE(invalid_effect.decision == cogito::Decision::Deny);
  REQUIRE(invalid_effect.reason_code == cogito::reason::kModeDenied);
}

TEST_CASE("PolicyEngine matches constraints and roles", "[policy][evaluate][constraints_roles]") {
  const auto j = ccj::Json::parse(R"json({
    "schema_version": 1,
    "default": "deny",
    "rules": [
      {
        "id": "rule-admin-only",
        "priority": 50,
        "tool": "admin.*",
        "roles": ["superadmin", "security_officer"],
        "decision": "allow",
        "reason": "Admin access for security officers"
      },
      {
        "id": "rule-constrained-calc",
        "priority": 40,
        "tool": "calc.div",
        "constraints": {
          "safe_mode": true
        },
        "decision": "allow",
        "reason": "Safe division allowed"
      }
    ]
  })json");

  auto engine = cogito::PolicyEngine::Create(j).take();

  SECTION("Role mismatch falls back to default deny") {
    const auto operator_subject = MakeSubject("user-1", {"operator", "viewer"});
    auto v = engine->Evaluate("admin.reset", cogito::Effect::None, ccj::Json::object(),
                              operator_subject, cogito::ExecutionMode::Default);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.reason_code == cogito::reason::kNoMatchingRule);
  }

  SECTION("Role match succeeds") {
    const auto officer_subject = MakeSubject("user-sec", {"operator", "security_officer"});
    auto v = engine->Evaluate("admin.reset", cogito::Effect::None, ccj::Json::object(),
                              officer_subject, cogito::ExecutionMode::Default);
    REQUIRE(v.decision == cogito::Decision::Allow);
    REQUIRE(v.rule_id == "rule-admin-only");
  }

  SECTION("Constraints match and mismatch") {
    const auto subject = MakeSubject("user-1");

    auto v_match = engine->Evaluate("calc.div", cogito::Effect::None,
                                    ccj::Json::parse(R"json({"safe_mode":true,"val":42})json"),
                                    subject, cogito::ExecutionMode::Default);
    REQUIRE(v_match.decision == cogito::Decision::Allow);
    REQUIRE(v_match.rule_id == "rule-constrained-calc");

    auto v_mismatch = engine->Evaluate("calc.div", cogito::Effect::None,
                                       ccj::Json::parse(R"json({"safe_mode":false,"val":42})json"),
                                       subject, cogito::ExecutionMode::Default);
    REQUIRE(v_mismatch.decision == cogito::Decision::Deny);
    REQUIRE(v_mismatch.reason_code == cogito::reason::kNoMatchingRule);

    const auto missing = engine->Evaluate(
        "calc.div", cogito::Effect::None, ccj::Json::object(), subject,
        cogito::ExecutionMode::Default);
    REQUIRE(missing.decision == cogito::Decision::Deny);
    REQUIRE(missing.reason_code == cogito::reason::kNoMatchingRule);

    const auto non_object = engine->Evaluate(
        "calc.div", cogito::Effect::None, ccj::Json::array(), subject,
        cogito::ExecutionMode::Default);
    REQUIRE(non_object.decision == cogito::Decision::Deny);
    REQUIRE(non_object.reason_code == cogito::reason::kNoMatchingRule);
  }
}

TEST_CASE("PolicyEngine defaults and digest are deterministic",
          "[policy][digest][default]") {
  cogito::PolicyRule canonical_rule;
  canonical_rule.id = "r1";
  canonical_rule.priority = 100;
  canonical_rule.tool = "motor.*";
  canonical_rule.decision = cogito::Decision::Allow;
  const ccj::Json expected_projection = ccj::Json::parse(R"json({
    "constraints":{},
    "decision":"allow",
    "effect_max":"",
    "effect_min":"",
    "id":"r1",
    "modes":[],
    "priority":100,
    "reason":"",
    "roles":[],
    "tool":"motor.*"
  })json");
  REQUIRE(cogito::NormalizePolicyRule(canonical_rule) == expected_projection);

  const ccj::Json golden_policy{
      {"schema_version", 1},
      {"default", "deny"},
      {"rules", ccj::Json::array({ccj::Json{
                    {"id", "r1"},
                    {"priority", 100},
                    {"tool", "motor.*"},
                    {"decision", "allow"},
                }})},
  };
  auto golden_engine = cogito::PolicyEngine::Create(golden_policy).take();
  REQUIRE(golden_engine->policy_digest().hex() ==
          "8133d790a08430801d9281846a6d09371bbedffd3b1d550e33229ac6614a00cf");

  auto explicit_defaults = golden_policy;
  explicit_defaults["rules"][0]["modes"] = ccj::Json::array();
  explicit_defaults["rules"][0]["roles"] = ccj::Json::array();
  explicit_defaults["rules"][0]["reason"] = "";
  explicit_defaults["rules"][0]["constraints"] = ccj::Json::object();
  auto explicit_defaults_engine =
      cogito::PolicyEngine::Create(explicit_defaults).take();
  REQUIRE(explicit_defaults_engine->policy_digest() ==
          golden_engine->policy_digest());

  const ccj::Json all_slots_policy = ccj::Json::parse(R"json({
    "schema_version":1,
    "default":"deny",
    "rules":[{
      "id":"r1",
      "priority":100,
      "tool":"motor.*",
      "decision":"allow",
      "effect_min":"none",
      "effect_max":"destructive",
      "modes":["default"],
      "roles":["operator"],
      "reason":"baseline",
      "constraints":{"line":"A"}
    }]
  })json");
  auto all_slots_engine = cogito::PolicyEngine::Create(all_slots_policy).take();
  std::vector<ccj::Json> slot_mutations(10U, all_slots_policy);
  slot_mutations[0]["rules"][0]["id"] = "r2";
  slot_mutations[1]["rules"][0]["priority"] = 101;
  slot_mutations[2]["rules"][0]["tool"] = "motor.stop";
  slot_mutations[3]["rules"][0]["decision"] = "ask";
  slot_mutations[4]["rules"][0]["effect_min"] = "write";
  slot_mutations[5]["rules"][0]["effect_max"] = "write";
  slot_mutations[6]["rules"][0]["modes"] = ccj::Json::array({"edit"});
  slot_mutations[7]["rules"][0]["roles"] = ccj::Json::array({"admin"});
  slot_mutations[8]["rules"][0]["reason"] = "changed";
  slot_mutations[9]["rules"][0]["constraints"] = ccj::Json{{"line", "B"}};
  for (std::size_t i = 0; i < slot_mutations.size(); ++i) {
    CAPTURE(i);
    auto mutated_slot_engine =
        cogito::PolicyEngine::Create(slot_mutations[i]).take();
    REQUIRE(mutated_slot_engine->policy_digest() !=
            all_slots_engine->policy_digest());
  }

  const auto first = ccj::Json::parse(R"json({
    "schema_version":1,
    "default":"deny",
    "rules":[
      {"id":"rule-b","priority":2,"tool":"beta","decision":"deny"},
      {"id":"rule-a","priority":2,"tool":"alpha","decision":"allow"}
    ]
  })json");
  const auto reordered = ccj::Json::parse(R"json({
    "schema_version":1,
    "default":"deny",
    "rules":[
      {"id":"rule-a","priority":2,"tool":"alpha","decision":"allow"},
      {"id":"rule-b","priority":2,"tool":"beta","decision":"deny"}
    ]
  })json");
  auto mutated = reordered;
  mutated["rules"][0]["reason"] = "changed";

  auto first_engine = cogito::PolicyEngine::Create(first).take();
  auto reordered_engine = cogito::PolicyEngine::Create(reordered).take();
  auto mutated_engine = cogito::PolicyEngine::Create(mutated).take();
  REQUIRE_FALSE(first_engine->policy_digest().is_zero());
  REQUIRE(first_engine->policy_digest() == reordered_engine->policy_digest());
  REQUIRE(first_engine->policy_digest() != mutated_engine->policy_digest());

  const cogito::Subject subject = MakeSubject("subject");
  for (const auto& entry : std::array<std::pair<const char*, cogito::Decision>, 3>{
           std::pair<const char*, cogito::Decision>{"deny", cogito::Decision::Deny},
           {"ask", cogito::Decision::Ask},
           {"allow", cogito::Decision::Allow},
       }) {
    const ccj::Json document{
        {"schema_version", 1}, {"default", entry.first}, {"rules", ccj::Json::array()}};
    auto engine = cogito::PolicyEngine::Create(document).take();
    const cogito::Verdict verdict = engine->Evaluate(
        "unmatched", cogito::Effect::None, ccj::Json::object(), subject,
        cogito::ExecutionMode::Default);
    REQUIRE(verdict.decision == entry.second);
    REQUIRE(verdict.gate_stage == 5);
    REQUIRE(verdict.rule_id.empty());
    REQUIRE(verdict.reason_code == cogito::reason::kNoMatchingRule);
    REQUIRE(verdict.policy_digest == engine->policy_digest());
  }
}

TEST_CASE("PolicyEngine tie-breaking resolves conflicts and equal decisions", "[policy][evaluate][tie_breaking]") {
  SECTION("Conflicting decisions at equal highest priority: Deny vs Allow -> Deny") {
    const auto j = ccj::Json::parse(R"json({
      "schema_version": 1,
      "default": "ask",
      "rules": [
        {
          "id": "rule-a-allow",
          "priority": 100,
          "tool": "device.operate",
          "decision": "allow",
          "reason": "Allow device"
        },
        {
          "id": "rule-b-deny",
          "priority": 100,
          "tool": "device.operate",
          "decision": "deny",
          "reason": "Deny device"
        }
      ]
    })json");

    auto engine = cogito::PolicyEngine::Create(j).take();
    auto v = engine->Evaluate("device.operate", cogito::Effect::None, ccj::Json::object(),
                              MakeSubject("u1"), cogito::ExecutionMode::Default);
    REQUIRE(v.decision == cogito::Decision::Deny);
    REQUIRE(v.rule_id == "");
    REQUIRE(v.reason_code == cogito::reason::kPolicyConflictDenied);
    REQUIRE(v.reason == "Policy conflict: multiple rules matched with differing decisions, resolved to Deny");
  }

  SECTION("Conflicting decisions at equal highest priority: Ask vs Allow -> Ask") {
    const auto j = ccj::Json::parse(R"json({
      "schema_version": 1,
      "default": "deny",
      "rules": [
        {
          "id": "rule-allow",
          "priority": 80,
          "tool": "device.run",
          "decision": "allow"
        },
        {
          "id": "rule-ask",
          "priority": 80,
          "tool": "device.run",
          "decision": "ask"
        }
      ]
    })json");

    auto engine = cogito::PolicyEngine::Create(j).take();
    auto v = engine->Evaluate("device.run", cogito::Effect::None, ccj::Json::object(),
                              MakeSubject("u1"), cogito::ExecutionMode::Default);
    REQUIRE(v.decision == cogito::Decision::Ask);
    REQUIRE(v.rule_id == "");
    REQUIRE(v.reason_code == cogito::reason::kApprovalRequired);
    REQUIRE(v.reason == "Policy conflict: multiple rules matched with differing decisions, resolved to Ask");
  }

  SECTION("Equal decisions at equal highest priority: smallest id wins") {
    const auto j = ccj::Json::parse(R"json({
      "schema_version": 1,
      "default": "deny",
      "rules": [
        {
          "id": "rule-zeta",
          "priority": 90,
          "tool": "calc.*",
          "decision": "allow",
          "reason": "Zeta rule reason"
        },
        {
          "id": "rule-alpha",
          "priority": 90,
          "tool": "calc.*",
          "decision": "allow",
          "reason": "Alpha rule reason"
        },
        {
          "id": "rule-beta",
          "priority": 90,
          "tool": "calc.*",
          "decision": "allow",
          "reason": "Beta rule reason"
        }
      ]
    })json");

    auto engine = cogito::PolicyEngine::Create(j).take();
    auto v = engine->Evaluate("calc.add", cogito::Effect::None, ccj::Json::object(),
                              MakeSubject("u1"), cogito::ExecutionMode::Default);
    REQUIRE(v.decision == cogito::Decision::Allow);
    REQUIRE(v.rule_id == "rule-alpha");
    REQUIRE(v.reason == "Alpha rule reason");
    REQUIRE(v.reason_code == cogito::reason::kAllowed);
  }

  SECTION("Equal decisions use unsigned UTF-8 byte order for rule ids") {
    const std::string utf8_id = "\xC3\xA9-rule";
    const ccj::Json document{
        {"schema_version", 1},
        {"default", "deny"},
        {"rules", ccj::Json::array({
                      ccj::Json{{"id", utf8_id},
                                {"priority", 50},
                                {"tool", "calc.*"},
                                {"decision", "allow"},
                                {"reason", "UTF-8 rule"}},
                      ccj::Json{{"id", "z-rule"},
                                {"priority", 50},
                                {"tool", "calc.*"},
                                {"decision", "allow"},
                                {"reason", "ASCII rule"}},
                  })},
    };
    auto engine = cogito::PolicyEngine::Create(document).take();
    const auto verdict = engine->Evaluate(
        "calc.add", cogito::Effect::None, ccj::Json::object(),
        MakeSubject("u1"), cogito::ExecutionMode::Default);
    REQUIRE(verdict.decision == cogito::Decision::Allow);
    REQUIRE(verdict.rule_id == "z-rule");
    REQUIRE(verdict.reason == "ASCII rule");
  }

  SECTION("Higher priority rule overrides lower priority rules completely") {
    const auto j = ccj::Json::parse(R"json({
      "schema_version": 1,
      "default": "deny",
      "rules": [
        {
          "id": "rule-prio-low-deny",
          "priority": 10,
          "tool": "motor.*",
          "decision": "deny",
          "reason": "Low prio deny"
        },
        {
          "id": "rule-prio-high-allow",
          "priority": 50,
          "tool": "motor.step",
          "decision": "allow",
          "reason": "High prio allow"
        }
      ]
    })json");

    auto engine = cogito::PolicyEngine::Create(j).take();
    auto v = engine->Evaluate("motor.step", cogito::Effect::Write, ccj::Json::object(),
                              MakeSubject("u1"), cogito::ExecutionMode::Default);
    REQUIRE(v.decision == cogito::Decision::Allow);
    REQUIRE(v.rule_id == "rule-prio-high-allow");
    REQUIRE(v.reason == "High prio allow");
  }

  SECTION("Three-way conflict resolves to Deny without a rule id") {
    const auto document = ccj::Json::parse(R"json({
      "schema_version":1,
      "default":"allow",
      "rules":[
        {"id":"allow","priority":7,"tool":"*","decision":"allow"},
        {"id":"ask","priority":7,"tool":"*","decision":"ask"},
        {"id":"deny","priority":7,"tool":"*","decision":"deny"}
      ]
    })json");
    auto engine = cogito::PolicyEngine::Create(document).take();
    const auto verdict = engine->Evaluate(
        "tool", cogito::Effect::None, ccj::Json::object(), MakeSubject("u1"),
        cogito::ExecutionMode::Default);
    REQUIRE(verdict.decision == cogito::Decision::Deny);
    REQUIRE(verdict.rule_id.empty());
    REQUIRE(verdict.reason_code == cogito::reason::kPolicyConflictDenied);
  }
}
