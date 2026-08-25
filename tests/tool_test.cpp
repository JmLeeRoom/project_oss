// SPDX-License-Identifier: Apache-2.0

#include "cogito/tool.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using Json = cogito::ccj::Json;

Json ObjectSchema() {
  return Json{{"type", "object"}, {"additionalProperties", false}};
}

void AttachHandler(cogito::ToolDescriptor& descriptor, std::size_t* call_count = nullptr) {
  descriptor.SetHandler(
      [call_count](const Json&, const cogito::ToolCallContext&) -> cogito::ToolResult {
        if (call_count != nullptr) {
          ++(*call_count);
        }
        cogito::ToolResult result;
        result.status = cogito::ToolResultStatus::Ok;
        result.content = Json::object();
        return result;
      });
}

cogito::ToolDescriptor ValidTool() {
  cogito::ToolDescriptor descriptor;
  descriptor.name = "test.tool";
  descriptor.input_schema = ObjectSchema();
  descriptor.output_schema = nullptr;
  descriptor.effect = cogito::Effect::None;
  descriptor.risk = cogito::Risk::Low;
  descriptor.idempotency = cogito::Idempotency::Safe;
  descriptor.approval_required = false;
  descriptor.timeout_ms = 3000;
  descriptor.max_output_bytes = 64U * 1024U;
  descriptor.provider_id = "test_provider";
  descriptor.invoker_id = "native_invoker";
  descriptor.status = cogito::ToolStatus::Enabled;
  AttachHandler(descriptor);
  return descriptor;
}

bool MatrixAllows(cogito::Effect effect,
                  cogito::Risk risk,
                  bool approval_required,
                  cogito::Idempotency idempotency) {
  switch (effect) {
    case cogito::Effect::None:
      return idempotency == cogito::Idempotency::Safe ||
             idempotency == cogito::Idempotency::Conditional;
    case cogito::Effect::Write:
      return risk != cogito::Risk::Low && approval_required &&
             (idempotency == cogito::Idempotency::Conditional ||
              idempotency == cogito::Idempotency::Unsafe);
    case cogito::Effect::Destructive:
      return (risk == cogito::Risk::High || risk == cogito::Risk::Critical) &&
             approval_required && idempotency == cogito::Idempotency::Unsafe;
  }
  return false;
}

void RequireContractViolation(const cogito::ToolDescriptor& descriptor) {
  const cogito::Error error = cogito::ValidateToolContract(descriptor);
  REQUIRE(error.code == cogito::Errc::ToolContractViolation);
}

}  // namespace

TEST_CASE("Tool enums have stable lowercase strings and deterministic invalid fallbacks",
          "[tool][enum]") {
  REQUIRE(std::string(cogito::ToString(cogito::Effect::None)) == "none");
  REQUIRE(std::string(cogito::ToString(cogito::Effect::Write)) == "write");
  REQUIRE(std::string(cogito::ToString(cogito::Effect::Destructive)) == "destructive");

  REQUIRE(std::string(cogito::ToString(cogito::Risk::Low)) == "low");
  REQUIRE(std::string(cogito::ToString(cogito::Risk::Medium)) == "medium");
  REQUIRE(std::string(cogito::ToString(cogito::Risk::High)) == "high");
  REQUIRE(std::string(cogito::ToString(cogito::Risk::Critical)) == "critical");

  REQUIRE(std::string(cogito::ToString(cogito::Idempotency::Safe)) == "safe");
  REQUIRE(std::string(cogito::ToString(cogito::Idempotency::Conditional)) == "conditional");
  REQUIRE(std::string(cogito::ToString(cogito::Idempotency::Unsafe)) == "unsafe");

  REQUIRE(std::string(cogito::ToString(cogito::ToolStatus::Enabled)) == "enabled");
  REQUIRE(std::string(cogito::ToString(cogito::ToolStatus::Forbidden)) == "forbidden");

  REQUIRE(std::string(cogito::ToString(cogito::ToolResultStatus::Ok)) == "ok");
  REQUIRE(std::string(cogito::ToString(cogito::ToolResultStatus::Error)) == "error");
  REQUIRE(std::string(cogito::ToString(cogito::ToolResultStatus::Timeout)) == "timeout");
  REQUIRE(std::string(cogito::ToString(cogito::ToolResultStatus::Cancelled)) == "cancelled");
  REQUIRE(std::string(cogito::ToString(cogito::ToolResultStatus::Indeterminate)) ==
          "indeterminate");

  REQUIRE(std::string(cogito::ToString(static_cast<cogito::Effect>(255U))) == "unknown");
  REQUIRE(std::string(cogito::ToString(static_cast<cogito::Risk>(255U))) == "unknown");
  REQUIRE(std::string(cogito::ToString(static_cast<cogito::Idempotency>(255U))) == "unknown");
  REQUIRE(std::string(cogito::ToString(static_cast<cogito::ToolStatus>(255U))) == "unknown");
  REQUIRE(std::string(cogito::ToString(static_cast<cogito::ToolResultStatus>(255U))) ==
          "unknown");
}

TEST_CASE("Tool contract enforces the complete effect risk approval idempotency matrix",
          "[tool][matrix]") {
  const std::array<cogito::Effect, 3> effects{
      cogito::Effect::None, cogito::Effect::Write, cogito::Effect::Destructive};
  const std::array<cogito::Risk, 4> risks{
      cogito::Risk::Low, cogito::Risk::Medium, cogito::Risk::High, cogito::Risk::Critical};
  const std::array<bool, 2> approvals{false, true};
  const std::array<cogito::Idempotency, 3> idempotencies{
      cogito::Idempotency::Safe, cogito::Idempotency::Conditional,
      cogito::Idempotency::Unsafe};

  std::size_t rows = 0U;
  for (const cogito::Effect effect : effects) {
    for (const cogito::Risk risk : risks) {
      for (const bool approval : approvals) {
        for (const cogito::Idempotency idempotency : idempotencies) {
          CAPTURE(static_cast<unsigned>(effect), static_cast<unsigned>(risk), approval,
                  static_cast<unsigned>(idempotency));
          cogito::ToolDescriptor descriptor = ValidTool();
          descriptor.effect = effect;
          descriptor.risk = risk;
          descriptor.approval_required = approval;
          descriptor.idempotency = idempotency;
          const cogito::Error result = cogito::ValidateToolContract(descriptor);
          REQUIRE(result.ok() == MatrixAllows(effect, risk, approval, idempotency));
          if (!result.ok()) {
            REQUIRE(result.code == cogito::Errc::ToolContractViolation);
          }
          ++rows;
        }
      }
    }
  }
  REQUIRE(rows == 72U);
}

TEST_CASE("Tool contract validates name and attribution identifier boundaries",
          "[tool][identifier]") {
  cogito::ToolDescriptor descriptor = ValidTool();

  const std::vector<std::string> valid_names{
      "a", "a_b9", "a.b", "a.b.c.d.e", std::string(128U, 'a')};
  for (const std::string& name : valid_names) {
    CAPTURE(name);
    descriptor.name = name;
    REQUIRE(cogito::ValidateToolContract(descriptor).ok());
  }

  const std::vector<std::string> invalid_names{
      "", ".a", "a.", "a..b", "A", "a-b", "a b", "a.b.c.d.e.f",
      std::string(129U, 'a'), u8"도구"};
  for (const std::string& name : invalid_names) {
    CAPTURE(name);
    descriptor.name = name;
    RequireContractViolation(descriptor);
  }

  descriptor = ValidTool();
  const std::vector<std::string> valid_ids{"a", "a_b-9", std::string(64U, 'a')};
  for (const std::string& id : valid_ids) {
    CAPTURE(id);
    descriptor.provider_id = id;
    descriptor.invoker_id = id;
    REQUIRE(cogito::ValidateToolContract(descriptor).ok());
  }

  const std::vector<std::string> invalid_ids{
      "", "A", "has space", "has.dot", u8"식별자", std::string(65U, 'a')};
  for (const std::string& id : invalid_ids) {
    CAPTURE(id);
    descriptor = ValidTool();
    descriptor.provider_id = id;
    RequireContractViolation(descriptor);
    descriptor = ValidTool();
    descriptor.invoker_id = id;
    RequireContractViolation(descriptor);
  }
}

TEST_CASE("Tool contract validates timeout and output byte boundaries", "[tool][limits]") {
  cogito::ToolDescriptor descriptor = ValidTool();
  for (const std::int32_t timeout : {1, 3'600'000}) {
    descriptor.timeout_ms = timeout;
    REQUIRE(cogito::ValidateToolContract(descriptor).ok());
  }
  for (const std::int32_t timeout : {-1, 0, 3'600'001}) {
    descriptor.timeout_ms = timeout;
    RequireContractViolation(descriptor);
  }

  descriptor = ValidTool();
  for (const std::size_t limit : {std::size_t{1U}, std::size_t{10'485'760U}}) {
    descriptor.max_output_bytes = limit;
    REQUIRE(cogito::ValidateToolContract(descriptor).ok());
  }
  for (const std::size_t limit : {std::size_t{0U}, std::size_t{10'485'761U}}) {
    descriptor.max_output_bytes = limit;
    RequireContractViolation(descriptor);
  }
}

TEST_CASE("Tool contract isolates enabled and forbidden lifecycle invariants",
          "[tool][status]") {
  cogito::ToolDescriptor descriptor = ValidTool();
  REQUIRE(cogito::ValidateToolContract(descriptor).ok());

  descriptor = ValidTool();
  descriptor.SetHandler({});
  RequireContractViolation(descriptor);

  descriptor = ValidTool();
  descriptor.input_schema = Json::array();
  RequireContractViolation(descriptor);

  descriptor = ValidTool();
  descriptor.forbidden_reason = "stale reason";
  RequireContractViolation(descriptor);

  descriptor = ValidTool();
  descriptor.status = cogito::ToolStatus::Forbidden;
  descriptor.forbidden_reason = "security_lockdown";
  descriptor.SetHandler({});
  REQUIRE(cogito::ValidateToolContract(descriptor).ok());

  descriptor.forbidden_reason.clear();
  RequireContractViolation(descriptor);

  descriptor.forbidden_reason = "security_lockdown";
  AttachHandler(descriptor);
  RequireContractViolation(descriptor);
}

TEST_CASE("Tool contract rejects every out of range descriptor enum", "[tool][enum]") {
  cogito::ToolDescriptor descriptor = ValidTool();
  descriptor.effect = static_cast<cogito::Effect>(255U);
  RequireContractViolation(descriptor);

  descriptor = ValidTool();
  descriptor.risk = static_cast<cogito::Risk>(255U);
  RequireContractViolation(descriptor);

  descriptor = ValidTool();
  descriptor.idempotency = static_cast<cogito::Idempotency>(255U);
  RequireContractViolation(descriptor);

  descriptor = ValidTool();
  descriptor.status = static_cast<cogito::ToolStatus>(255U);
  RequireContractViolation(descriptor);
}
