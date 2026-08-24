// SPDX-License-Identifier: Apache-2.0

#include "cogito/digest.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

cogito::ccj::Json StrictJson(std::string_view text) {
  auto parsed = cogito::ccj::ParseStrict(text);
  if (!parsed) {
    throw std::runtime_error("test fixture is not valid strict JSON");
  }
  return std::move(parsed).take();
}

cogito::Digest FilledDigest(std::uint8_t value) {
  cogito::Digest digest;
  digest.bytes.fill(value);
  return digest;
}

void RequireDigest(const cogito::Result<cogito::Digest>& actual,
                   const std::string& expected) {
  REQUIRE(actual.ok());
  REQUIRE(actual.value().hex() == expected);
}

void RequireDifferent(std::string_view field,
                      const cogito::Result<cogito::Digest>& baseline,
                      const cogito::Result<cogito::Digest>& mutated) {
  INFO("mutated field: " << field);
  REQUIRE(baseline.ok());
  REQUIRE(mutated.ok());
  REQUIRE(baseline.value() != mutated.value());
}

void AppendU32Le(std::vector<std::uint8_t>& output, std::uint32_t value) {
  for (unsigned int shift = 0; shift < 32U; shift += 8U) {
    output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
  }
}

void AppendU64Le(std::vector<std::uint8_t>& output, std::uint64_t value) {
  for (unsigned int shift = 0; shift < 64U; shift += 8U) {
    output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
  }
}

void AppendLengthPrefixed(std::vector<std::uint8_t>& output, std::string_view value) {
  AppendU32Le(output, static_cast<std::uint32_t>(value.size()));
  output.insert(output.end(), value.begin(), value.end());
}

cogito::ToolProjectionDto GoldenTool() {
  cogito::ToolProjectionDto tool;
  tool.name = "a.tool";
  tool.status = "enabled";
  tool.forbidden_reason = "";
  tool.toolschema_digest = FilledDigest(0x04U);
  tool.grammar_coverage = "full";
  tool.effect = "none";
  tool.risk = "low";
  tool.idempotency = "safe";
  tool.approval_required = 0;
  tool.timeout_ms = 3000;
  tool.max_output_bytes = 65536;
  tool.provider_id = "p1";
  tool.invoker_id = "i1";
  return tool;
}

cogito::PolicyRuleProjectionDto GoldenRule() {
  cogito::PolicyRuleProjectionDto rule;
  rule.priority = 100;
  rule.rule_id = "r1";
  rule.normalized_rule = StrictJson(R"json({"action":"allow"})json");
  return rule;
}

}  // namespace

TEST_CASE("All nine digest domain tags are unique and versioned", "[digest][domain]") {
  const std::array<std::string_view, 9> tags{{
      cogito::domain::kAction,     cogito::domain::kOperation,
      cogito::domain::kPermit,     cogito::domain::kAudit,
      cogito::domain::kToolSchema, cogito::domain::kRegistry,
      cogito::domain::kPolicy,     cogito::domain::kConfig,
      cogito::domain::kModel,
  }};
  std::unordered_set<std::string_view> unique;
  for (const std::string_view tag : tags) {
    REQUIRE(tag.size() > 3U);
    REQUIRE(tag.substr(tag.size() - 3U) == "-v1");
    REQUIRE(unique.insert(tag).second);
  }
  REQUIRE(unique.size() == tags.size());
}

TEST_CASE("OpenSSL EVP SHA-256 matches standard vectors", "[digest][sha256]") {
  RequireDigest(cogito::Sha256(nullptr, 0U),
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  RequireDigest(cogito::Sha256(std::string_view{"abc"}),
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  auto invalid = cogito::Sha256(nullptr, 1U);
  REQUIRE_FALSE(invalid.ok());
  REQUIRE(invalid.error().code == cogito::Errc::InvalidArgument);
}

TEST_CASE("LpBuffer emits typed little-endian fields in exact order", "[digest][lp]") {
  auto created = cogito::LpBuffer::Create("d");
  REQUIRE(created.ok());
  cogito::LpBuffer buffer = std::move(created).take();

  const std::array<std::uint8_t, 2> raw{{0xFFU, 0x00U}};
  cogito::Digest digest;
  for (std::size_t i = 0; i < digest.bytes.size(); ++i) {
    digest.bytes[i] = static_cast<std::uint8_t>(i);
  }
  REQUIRE(buffer.AppendString("ab").ok());
  REQUIRE(buffer.AppendBytes(raw.data(), raw.size()).ok());
  REQUIRE(buffer.AppendU64(0x0102030405060708ULL).ok());
  REQUIRE(buffer.AppendDigest(digest).ok());
  REQUIRE(buffer.AppendJson(StrictJson(R"json({"b":1,"a":2})json")).ok());

  std::vector<std::uint8_t> expected;
  AppendLengthPrefixed(expected, "d");
  AppendLengthPrefixed(expected, "ab");
  AppendU32Le(expected, 2U);
  expected.insert(expected.end(), raw.begin(), raw.end());
  AppendU64Le(expected, 0x0102030405060708ULL);
  expected.insert(expected.end(), digest.bytes.begin(), digest.bytes.end());
  AppendLengthPrefixed(expected, R"json({"a":2,"b":1})json");
  REQUIRE(buffer.bytes() == expected);
  REQUIRE(buffer.ComputeDigest().ok());
}

TEST_CASE("LpBuffer safely snapshots aliased input before growth", "[digest][lp]") {
  auto created = cogito::LpBuffer::Create("self-alias-domain");
  REQUIRE(created.ok());
  cogito::LpBuffer buffer = std::move(created).take();
  const std::vector<std::uint8_t> original = buffer.bytes();

  REQUIRE(buffer.AppendBytes(buffer.bytes().data(), buffer.bytes().size()).ok());
  std::vector<std::uint8_t> expected = original;
  AppendU32Le(expected, static_cast<std::uint32_t>(original.size()));
  expected.insert(expected.end(), original.begin(), original.end());
  REQUIRE(buffer.bytes() == expected);
}

TEST_CASE("LpBuffer is fail-closed and sticky without mutating prior bytes",
          "[digest][lp][negative]") {
  REQUIRE_FALSE(cogito::LpBuffer::Create("").ok());

  auto created = cogito::LpBuffer::Create("test-domain");
  REQUIRE(created.ok());
  cogito::LpBuffer buffer = std::move(created).take();
  const std::vector<std::uint8_t> before = buffer.bytes();

  std::string invalid_utf8;
  invalid_utf8.push_back(static_cast<char>(0xC0));
  invalid_utf8.push_back(static_cast<char>(0xAF));
  const cogito::Error first = buffer.AppendString(invalid_utf8);
  REQUIRE(first.code == cogito::Errc::NotUtf8);
  REQUIRE(buffer.bytes() == before);
  REQUIRE_FALSE(buffer.ok());

  const cogito::Error second = buffer.AppendU64(7U);
  REQUIRE(second.code == cogito::Errc::NotUtf8);
  REQUIRE(buffer.bytes() == before);
  REQUIRE(buffer.last_error().code == cogito::Errc::NotUtf8);
  auto digest = buffer.ComputeDigest();
  REQUIRE_FALSE(digest.ok());
  REQUIRE(digest.error().code == cogito::Errc::NotUtf8);

  auto null_created = cogito::LpBuffer::Create("test-domain");
  REQUIRE(null_created.ok());
  cogito::LpBuffer null_buffer = std::move(null_created).take();
  const std::vector<std::uint8_t> null_before = null_buffer.bytes();
  const cogito::Error null_error = null_buffer.AppendBytes(nullptr, 1U);
  REQUIRE(null_error.code == cogito::Errc::InvalidArgument);
  REQUIRE(null_buffer.bytes() == null_before);
}

TEST_CASE("All nine approved digest projections match official golden vectors",
          "[digest][golden]") {
  const auto arguments = StrictJson(R"json({"arg":1})json");
  RequireDigest(cogito::ComputeActionDigest("sess-001", 1U, "act-001", "test.tool",
                                            arguments),
                "bc22fa11f50ecbf4ae8befc96ef912732d4e23a2531506c428f5e0d0c3b075c8");
  RequireDigest(cogito::ComputeOperationDigest("test.tool", arguments),
                "a087d03801d9e9f4533d9e2375283dda8d7762de5a6d767df85d671acae25ab1");
  RequireDigest(cogito::ComputePermitDigest(FilledDigest(0x01U), "user-1", 2U,
                                            FilledDigest(0x02U), FilledDigest(0x03U)),
                "11d7e38abac240d1bbf13ef7b7ff662ccc57ebd231923069179f53985324c739");
  RequireDigest(cogito::ComputeAuditDigest(
                    FilledDigest(0x00U), "evt-001", "sess-001", 1U, "act-001",
                    "2026-08-24T00:00:00Z", 1000000U, "epoch-001", "tool_result", 1U,
                    "agent", StrictJson(R"json({"status":"ok"})json"), 1U),
                "a20a0bc1a0fe979ad6d988cc92af9a91294a80b2efb2f289180d472b6e065085");
  RequireDigest(cogito::ComputeToolSchemaDigest(
                    "test.tool", StrictJson(R"json({"type":"object"})json"), nullptr),
                "9bef4d123b8c74a87f0cf1d91bb8f1186545854f91ba10777a6b6f38db295e64");
  RequireDigest(cogito::ComputeRegistryDigest({GoldenTool()}),
                "9203f42851aeea36e00bbb0a824032b4ae8c8692b4efa08c9192523239eeb786");
  RequireDigest(cogito::ComputePolicyDigest(1U, "deny", {GoldenRule()}),
                "45e5f703f7e00563042b4f14e1adcb38fb8211f5faada74e00976b0a27c7d51a");
  RequireDigest(cogito::ComputeConfigDigest(
                    1U, StrictJson(R"json({"mode":"readonly"})json")),
                "91daa0313f6836bd456339804217c1fbc2ea64c56157133ad84031c08b081737");
  RequireDigest(cogito::ComputeModelDigest("prov-1", "mod-1", "w-sha", "ct-d", "tok-d",
                                           "q4_k_m"),
                "a0cb8345b7fe36e8850a958968a2a602c3b733f229028eafab2292c877b98963");
}

TEST_CASE("Every field in all nine digest projections is bound to its result",
          "[digest][mutation]") {
  const auto arguments = StrictJson(R"json({"arg":1})json");
  const auto changed_arguments = StrictJson(R"json({"arg":2})json");

  const auto action =
      cogito::ComputeActionDigest("sess-001", 1U, "act-001", "test.tool", arguments);
  RequireDifferent("action.session_id", action,
                   cogito::ComputeActionDigest("sess-002", 1U, "act-001",
                                               "test.tool", arguments));
  RequireDifferent("action.turn_id", action,
                   cogito::ComputeActionDigest("sess-001", 2U, "act-001",
                                               "test.tool", arguments));
  RequireDifferent("action.action_id", action,
                   cogito::ComputeActionDigest("sess-001", 1U, "act-002",
                                               "test.tool", arguments));
  RequireDifferent("action.tool_name", action,
                   cogito::ComputeActionDigest("sess-001", 1U, "act-001",
                                               "test.other", arguments));
  RequireDifferent("action.arguments", action,
                   cogito::ComputeActionDigest("sess-001", 1U, "act-001",
                                               "test.tool", changed_arguments));

  const auto operation = cogito::ComputeOperationDigest("test.tool", arguments);
  RequireDifferent("operation.tool_name", operation,
                   cogito::ComputeOperationDigest("test.other", arguments));
  RequireDifferent("operation.arguments", operation,
                   cogito::ComputeOperationDigest("test.tool", changed_arguments));

  const auto permit = cogito::ComputePermitDigest(
      FilledDigest(0x01U), "user-1", 2U, FilledDigest(0x02U), FilledDigest(0x03U));
  RequireDifferent("permit.action_digest", permit,
                   cogito::ComputePermitDigest(FilledDigest(0x04U), "user-1", 2U,
                                               FilledDigest(0x02U), FilledDigest(0x03U)));
  RequireDifferent("permit.subject_id", permit,
                   cogito::ComputePermitDigest(FilledDigest(0x01U), "user-2", 2U,
                                               FilledDigest(0x02U), FilledDigest(0x03U)));
  RequireDifferent("permit.mode", permit,
                   cogito::ComputePermitDigest(FilledDigest(0x01U), "user-1", 3U,
                                               FilledDigest(0x02U), FilledDigest(0x03U)));
  RequireDifferent("permit.policy_digest", permit,
                   cogito::ComputePermitDigest(FilledDigest(0x01U), "user-1", 2U,
                                               FilledDigest(0x04U), FilledDigest(0x03U)));
  RequireDifferent("permit.registry_digest", permit,
                   cogito::ComputePermitDigest(FilledDigest(0x01U), "user-1", 2U,
                                               FilledDigest(0x02U), FilledDigest(0x04U)));

  const auto payload = StrictJson(R"json({"status":"ok"})json");
  const auto changed_payload = StrictJson(R"json({"status":"changed"})json");
  const auto audit = cogito::ComputeAuditDigest(
      FilledDigest(0x00U), "evt-001", "sess-001", 1U, "act-001",
      "2026-08-24T00:00:00Z", 1000000U, "epoch-001", "tool_result", 1U,
      "agent", payload, 1U);
  RequireDifferent("audit.prev_hash", audit,
                   cogito::ComputeAuditDigest(
                       FilledDigest(0x01U), "evt-001", "sess-001", 1U,
                       "act-001", "2026-08-24T00:00:00Z", 1000000U,
                       "epoch-001", "tool_result", 1U, "agent", payload, 1U));
  RequireDifferent("audit.event_id", audit,
                   cogito::ComputeAuditDigest(
                       FilledDigest(0x00U), "evt-002", "sess-001", 1U,
                       "act-001", "2026-08-24T00:00:00Z", 1000000U,
                       "epoch-001", "tool_result", 1U, "agent", payload, 1U));
  RequireDifferent("audit.session_id", audit,
                   cogito::ComputeAuditDigest(
                       FilledDigest(0x00U), "evt-001", "sess-002", 1U,
                       "act-001", "2026-08-24T00:00:00Z", 1000000U,
                       "epoch-001", "tool_result", 1U, "agent", payload, 1U));
  RequireDifferent("audit.turn_id", audit,
                   cogito::ComputeAuditDigest(
                       FilledDigest(0x00U), "evt-001", "sess-001", 2U,
                       "act-001", "2026-08-24T00:00:00Z", 1000000U,
                       "epoch-001", "tool_result", 1U, "agent", payload, 1U));
  RequireDifferent("audit.action_id", audit,
                   cogito::ComputeAuditDigest(
                       FilledDigest(0x00U), "evt-001", "sess-001", 1U,
                       "act-002", "2026-08-24T00:00:00Z", 1000000U,
                       "epoch-001", "tool_result", 1U, "agent", payload, 1U));
  RequireDifferent("audit.wall_time_utc", audit,
                   cogito::ComputeAuditDigest(
                       FilledDigest(0x00U), "evt-001", "sess-001", 1U,
                       "act-001", "2026-08-24T00:00:01Z", 1000000U,
                       "epoch-001", "tool_result", 1U, "agent", payload, 1U));
  RequireDifferent("audit.monotonic_ns", audit,
                   cogito::ComputeAuditDigest(
                       FilledDigest(0x00U), "evt-001", "sess-001", 1U,
                       "act-001", "2026-08-24T00:00:00Z", 1000001U,
                       "epoch-001", "tool_result", 1U, "agent", payload, 1U));
  RequireDifferent("audit.process_epoch_id", audit,
                   cogito::ComputeAuditDigest(
                       FilledDigest(0x00U), "evt-001", "sess-001", 1U,
                       "act-001", "2026-08-24T00:00:00Z", 1000000U,
                       "epoch-002", "tool_result", 1U, "agent", payload, 1U));
  RequireDifferent("audit.kind", audit,
                   cogito::ComputeAuditDigest(
                       FilledDigest(0x00U), "evt-001", "sess-001", 1U,
                       "act-001", "2026-08-24T00:00:00Z", 1000000U,
                       "epoch-001", "approval", 1U, "agent", payload, 1U));
  RequireDifferent("audit.actor_type", audit,
                   cogito::ComputeAuditDigest(
                       FilledDigest(0x00U), "evt-001", "sess-001", 1U,
                       "act-001", "2026-08-24T00:00:00Z", 1000000U,
                       "epoch-001", "tool_result", 2U, "agent", payload, 1U));
  RequireDifferent("audit.actor_id", audit,
                   cogito::ComputeAuditDigest(
                       FilledDigest(0x00U), "evt-001", "sess-001", 1U,
                       "act-001", "2026-08-24T00:00:00Z", 1000000U,
                       "epoch-001", "tool_result", 1U, "operator", payload, 1U));
  RequireDifferent("audit.payload", audit,
                   cogito::ComputeAuditDigest(
                       FilledDigest(0x00U), "evt-001", "sess-001", 1U,
                       "act-001", "2026-08-24T00:00:00Z", 1000000U,
                       "epoch-001", "tool_result", 1U, "agent", changed_payload, 1U));
  RequireDifferent("audit.schema_version", audit,
                   cogito::ComputeAuditDigest(
                       FilledDigest(0x00U), "evt-001", "sess-001", 1U,
                       "act-001", "2026-08-24T00:00:00Z", 1000000U,
                       "epoch-001", "tool_result", 1U, "agent", payload, 2U));

  const auto input_schema = StrictJson(R"json({"type":"object"})json");
  const auto output_schema = StrictJson(R"json({"type":"string"})json");
  const auto tool_schema =
      cogito::ComputeToolSchemaDigest("test.tool", input_schema, output_schema);
  RequireDifferent("toolschema.name", tool_schema,
                   cogito::ComputeToolSchemaDigest("test.other", input_schema,
                                                   output_schema));
  RequireDifferent("toolschema.input_schema", tool_schema,
                   cogito::ComputeToolSchemaDigest(
                       "test.tool", StrictJson(R"json({"type":"array"})json"),
                       output_schema));
  RequireDifferent("toolschema.output_schema", tool_schema,
                   cogito::ComputeToolSchemaDigest("test.tool", input_schema, nullptr));

  const auto registry = cogito::ComputeRegistryDigest({GoldenTool()});
  const auto check_tool = [&](std::string_view field, cogito::ToolProjectionDto tool) {
    RequireDifferent(field, registry, cogito::ComputeRegistryDigest({std::move(tool)}));
  };
  auto tool = GoldenTool();
  tool.name = "b.tool";
  check_tool("registry.name", std::move(tool));
  tool = GoldenTool();
  tool.status = "forbidden";
  tool.forbidden_reason = "blocked";
  check_tool("registry.status", std::move(tool));
  auto forbidden_a = GoldenTool();
  forbidden_a.status = "forbidden";
  forbidden_a.forbidden_reason = "reason-a";
  auto forbidden_b = forbidden_a;
  forbidden_b.forbidden_reason = "reason-b";
  RequireDifferent("registry.forbidden_reason",
                   cogito::ComputeRegistryDigest({std::move(forbidden_a)}),
                   cogito::ComputeRegistryDigest({std::move(forbidden_b)}));
  tool = GoldenTool();
  tool.toolschema_digest = FilledDigest(0x05U);
  check_tool("registry.toolschema_digest", std::move(tool));
  tool = GoldenTool();
  tool.grammar_coverage = "partial";
  check_tool("registry.grammar_coverage", std::move(tool));
  tool = GoldenTool();
  tool.effect = "write";
  check_tool("registry.effect", std::move(tool));
  tool = GoldenTool();
  tool.risk = "medium";
  check_tool("registry.risk", std::move(tool));
  tool = GoldenTool();
  tool.idempotency = "conditional";
  check_tool("registry.idempotency", std::move(tool));
  tool = GoldenTool();
  tool.approval_required = 1U;
  check_tool("registry.approval_required", std::move(tool));
  tool = GoldenTool();
  tool.timeout_ms += 1U;
  check_tool("registry.timeout_ms", std::move(tool));
  tool = GoldenTool();
  tool.max_output_bytes += 1U;
  check_tool("registry.max_output_bytes", std::move(tool));
  tool = GoldenTool();
  tool.provider_id = "p2";
  check_tool("registry.provider_id", std::move(tool));
  tool = GoldenTool();
  tool.invoker_id = "i2";
  check_tool("registry.invoker_id", std::move(tool));

  const auto policy = cogito::ComputePolicyDigest(1U, "deny", {GoldenRule()});
  RequireDifferent("policy.schema_version", policy,
                   cogito::ComputePolicyDigest(2U, "deny", {GoldenRule()}));
  RequireDifferent("policy.default_decision", policy,
                   cogito::ComputePolicyDigest(1U, "allow", {GoldenRule()}));
  auto rule = GoldenRule();
  rule.priority += 1U;
  RequireDifferent("policy.rule.priority", policy,
                   cogito::ComputePolicyDigest(1U, "deny", {rule}));
  rule = GoldenRule();
  rule.rule_id = "r2";
  RequireDifferent("policy.rule.rule_id", policy,
                   cogito::ComputePolicyDigest(1U, "deny", {rule}));
  rule = GoldenRule();
  rule.normalized_rule = StrictJson(R"json({"action":"deny"})json");
  RequireDifferent("policy.rule.normalized_rule", policy,
                   cogito::ComputePolicyDigest(1U, "deny", {rule}));

  const auto config =
      cogito::ComputeConfigDigest(1U, StrictJson(R"json({"mode":"readonly"})json"));
  RequireDifferent("config.schema_version", config,
                   cogito::ComputeConfigDigest(
                       2U, StrictJson(R"json({"mode":"readonly"})json")));
  RequireDifferent("config.normalized_config", config,
                   cogito::ComputeConfigDigest(
                       1U, StrictJson(R"json({"mode":"active"})json")));

  const auto model =
      cogito::ComputeModelDigest("prov-1", "mod-1", "w-sha", "ct-d", "tok-d",
                                 "q4_k_m");
  RequireDifferent("model.provider_id", model,
                   cogito::ComputeModelDigest("prov-2", "mod-1", "w-sha", "ct-d",
                                              "tok-d", "q4_k_m"));
  RequireDifferent("model.model_id", model,
                   cogito::ComputeModelDigest("prov-1", "mod-2", "w-sha", "ct-d",
                                              "tok-d", "q4_k_m"));
  RequireDifferent("model.weights_sha256", model,
                   cogito::ComputeModelDigest("prov-1", "mod-1", "w-sha-2", "ct-d",
                                              "tok-d", "q4_k_m"));
  RequireDifferent("model.chat_template_digest", model,
                   cogito::ComputeModelDigest("prov-1", "mod-1", "w-sha", "ct-d-2",
                                              "tok-d", "q4_k_m"));
  RequireDifferent("model.tokenizer_digest", model,
                   cogito::ComputeModelDigest("prov-1", "mod-1", "w-sha", "ct-d",
                                              "tok-d-2", "q4_k_m"));
  RequireDifferent("model.quantization", model,
                   cogito::ComputeModelDigest("prov-1", "mod-1", "w-sha", "ct-d",
                                              "tok-d", "q8_0"));
}

TEST_CASE("Digest domain separation and empty-field framing are unambiguous",
          "[digest][domain][lp]") {
  const std::array<std::string_view, 9> tags{{
      cogito::domain::kAction,     cogito::domain::kOperation,
      cogito::domain::kPermit,     cogito::domain::kAudit,
      cogito::domain::kToolSchema, cogito::domain::kRegistry,
      cogito::domain::kPolicy,     cogito::domain::kConfig,
      cogito::domain::kModel,
  }};
  std::unordered_set<std::string> digests;
  for (const std::string_view tag : tags) {
    auto created = cogito::LpBuffer::Create(tag);
    REQUIRE(created.ok());
    cogito::LpBuffer buffer = std::move(created).take();
    REQUIRE(buffer.AppendString("same-payload").ok());
    auto digest = buffer.ComputeDigest();
    REQUIRE(digest.ok());
    REQUIRE(digests.insert(digest.value().hex()).second);
  }

  auto absent_created = cogito::LpBuffer::Create("lp-ambiguity-v1");
  auto empty_created = cogito::LpBuffer::Create("lp-ambiguity-v1");
  REQUIRE(absent_created.ok());
  REQUIRE(empty_created.ok());
  cogito::LpBuffer absent = std::move(absent_created).take();
  cogito::LpBuffer empty = std::move(empty_created).take();
  REQUIRE(empty.AppendString("").ok());
  RequireDifferent("empty field versus absent field", absent.ComputeDigest(),
                   empty.ComputeDigest());
}

TEST_CASE("Registry and policy projections sort deterministically and reject duplicates",
          "[digest][projection]") {
  auto tool_a = GoldenTool();
  auto tool_b = GoldenTool();
  tool_b.name = "b.tool";
  tool_b.toolschema_digest = FilledDigest(0x05U);

  auto registry_ab = cogito::ComputeRegistryDigest({tool_a, tool_b});
  auto registry_ba = cogito::ComputeRegistryDigest({tool_b, tool_a});
  REQUIRE(registry_ab.ok());
  REQUIRE(registry_ba.ok());
  REQUIRE(registry_ab.value() == registry_ba.value());

  auto registry_duplicate = cogito::ComputeRegistryDigest({tool_a, tool_a});
  REQUIRE_FALSE(registry_duplicate.ok());
  REQUIRE(registry_duplicate.error().code == cogito::Errc::DuplicateKey);

  auto invalid_tool = GoldenTool();
  invalid_tool.effect = "unknown";
  auto invalid_registry = cogito::ComputeRegistryDigest({invalid_tool});
  REQUIRE_FALSE(invalid_registry.ok());
  REQUIRE(invalid_registry.error().code == cogito::Errc::InvalidArgument);

  auto rule_a = GoldenRule();
  auto rule_b = GoldenRule();
  rule_b.priority = 200U;
  rule_b.rule_id = "r2";
  auto policy_ab = cogito::ComputePolicyDigest(1U, "deny", {rule_a, rule_b});
  auto policy_ba = cogito::ComputePolicyDigest(1U, "deny", {rule_b, rule_a});
  REQUIRE(policy_ab.ok());
  REQUIRE(policy_ba.ok());
  REQUIRE(policy_ab.value() == policy_ba.value());

  auto ascii_rule = GoldenRule();
  ascii_rule.priority = 100U;
  ascii_rule.rule_id = "z";
  ascii_rule.normalized_rule = StrictJson(R"json({"which":"ascii"})json");
  auto utf8_rule = GoldenRule();
  utf8_rule.priority = 100U;
  utf8_rule.rule_id = u8"é";
  utf8_rule.normalized_rule = StrictJson(R"json({"which":"utf8"})json");

  auto tied_forward =
      cogito::ComputePolicyDigest(1U, "deny", {ascii_rule, utf8_rule});
  auto tied_reverse =
      cogito::ComputePolicyDigest(1U, "deny", {utf8_rule, ascii_rule});
  REQUIRE(tied_forward.ok());
  REQUIRE(tied_reverse.ok());
  REQUIRE(tied_forward.value() == tied_reverse.value());

  auto expected_buffer = cogito::LpBuffer::Create(cogito::domain::kPolicy);
  REQUIRE(expected_buffer.ok());
  cogito::LpBuffer expected = std::move(expected_buffer).take();
  REQUIRE(expected.AppendU64(1U).ok());
  REQUIRE(expected.AppendString("deny").ok());
  REQUIRE(expected.AppendU64(ascii_rule.priority).ok());
  REQUIRE(expected.AppendString(ascii_rule.rule_id).ok());
  REQUIRE(expected.AppendJson(ascii_rule.normalized_rule).ok());
  REQUIRE(expected.AppendU64(utf8_rule.priority).ok());
  REQUIRE(expected.AppendString(utf8_rule.rule_id).ok());
  REQUIRE(expected.AppendJson(utf8_rule.normalized_rule).ok());
  REQUIRE(expected.ComputeDigest().ok());
  REQUIRE(tied_forward.value() == expected.ComputeDigest().value());

  auto policy_duplicate = cogito::ComputePolicyDigest(1U, "deny", {rule_a, rule_a});
  REQUIRE_FALSE(policy_duplicate.ok());
  REQUIRE(policy_duplicate.error().code == cogito::Errc::DuplicateKey);
}
