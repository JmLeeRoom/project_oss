// SPDX-License-Identifier: Apache-2.0

#include "cogito/registry.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
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

cogito::ToolDescriptor EnabledTool(std::string name,
                                   cogito::Effect effect = cogito::Effect::None,
                                   std::size_t* call_count = nullptr) {
  cogito::ToolDescriptor descriptor;
  descriptor.name = std::move(name);
  descriptor.description = "test tool";
  descriptor.input_schema = ObjectSchema();
  descriptor.output_schema = nullptr;
  descriptor.effect = effect;
  descriptor.risk = effect == cogito::Effect::None
                        ? cogito::Risk::Low
                        : (effect == cogito::Effect::Write ? cogito::Risk::Medium
                                                           : cogito::Risk::High);
  descriptor.idempotency = effect == cogito::Effect::None
                               ? cogito::Idempotency::Safe
                               : (effect == cogito::Effect::Write
                                      ? cogito::Idempotency::Conditional
                                      : cogito::Idempotency::Unsafe);
  descriptor.approval_required = effect != cogito::Effect::None;
  descriptor.timeout_ms = 3000;
  descriptor.max_output_bytes = 64U * 1024U;
  descriptor.provider_id = "test_provider";
  descriptor.invoker_id = "native_invoker";
  descriptor.status = cogito::ToolStatus::Enabled;
  AttachHandler(descriptor, call_count);
  return descriptor;
}

cogito::ToolDescriptor ForbiddenTool(std::string name) {
  cogito::ToolDescriptor descriptor;
  descriptor.name = std::move(name);
  descriptor.description = "forbidden tool";
  descriptor.input_schema = ObjectSchema();
  descriptor.output_schema = nullptr;
  descriptor.effect = cogito::Effect::Destructive;
  descriptor.risk = cogito::Risk::Critical;
  descriptor.idempotency = cogito::Idempotency::Unsafe;
  descriptor.approval_required = true;
  descriptor.timeout_ms = 5000;
  descriptor.max_output_bytes = 64U * 1024U;
  descriptor.provider_id = "test_provider";
  descriptor.invoker_id = "native_invoker";
  descriptor.status = cogito::ToolStatus::Forbidden;
  descriptor.forbidden_reason = "security_lockdown";
  return descriptor;
}

class VectorProvider final : public cogito::ToolProvider {
 public:
  explicit VectorProvider(std::string id) : id_(std::move(id)) {}

  const char* provider_id() const noexcept override {
    return null_id_ ? nullptr : id_.c_str();
  }

  cogito::Result<std::vector<cogito::ToolDescriptor>> Describe() override {
    ++describe_calls_;
    if (error_.has_value()) {
      return *error_;
    }
    return tools_;
  }

  std::string id_;
  std::vector<cogito::ToolDescriptor> tools_;
  std::optional<cogito::Error> error_;
  bool null_id_ = false;
  std::size_t describe_calls_ = 0U;
};

cogito::ToolDescriptor CalcTool() {
  cogito::ToolDescriptor descriptor = EnabledTool("calc.add");
  descriptor.description = "Add two integers";
  descriptor.input_schema =
      Json{{"$schema", "http://json-schema.org/draft-07/schema#"},
           {"type", "object"},
           {"properties",
            {{"a", Json{{"type", "integer"}}}, {"b", Json{{"type", "integer"}}}}},
           {"required", Json::array({"a", "b"})},
           {"additionalProperties", false}};
  descriptor.provider_id = "math_prov";
  return descriptor;
}

cogito::ToolDescriptor FsDeleteTool() {
  cogito::ToolDescriptor descriptor = ForbiddenTool("fs.delete");
  descriptor.description = "Delete file";
  descriptor.input_schema =
      Json{{"$schema", "http://json-schema.org/draft-07/schema#"},
           {"type", "object"},
           {"properties", {{"path", Json{{"type", "string"}, {"maxLength", 256}}}}},
           {"required", Json::array({"path"})},
           {"additionalProperties", false}};
  descriptor.provider_id = "fs_prov";
  return descriptor;
}

std::vector<std::string> ExportedNames(const cogito::ToolRegistry& registry,
                                       cogito::ExecutionMode mode) {
  std::vector<std::string> names;
  for (const cogito::ModelToolDeclaration& declaration : registry.ExportForModel(mode)) {
    names.push_back(declaration.name);
  }
  return names;
}

}  // namespace

TEST_CASE("LookupKind has stable strings and invalid fallback", "[registry][enum]") {
  REQUIRE(std::string(cogito::ToString(cogito::LookupKind::Absent)) == "absent");
  REQUIRE(std::string(cogito::ToString(cogito::LookupKind::Enabled)) == "enabled");
  REQUIRE(std::string(cogito::ToString(cogito::LookupKind::Forbidden)) == "forbidden");
  REQUIRE(std::string(cogito::ToString(static_cast<cogito::LookupKind>(255U))) == "unknown");
}

TEST_CASE("Register rejects duplicate and invalid descriptors without state changes",
          "[registry][register]") {
  cogito::ToolRegistry registry;
  REQUIRE(registry.Register(EnabledTool("alpha.tool")).ok());
  const cogito::LookupResult before = registry.Lookup("alpha.tool");
  REQUIRE(before.kind == cogito::LookupKind::Enabled);
  REQUIRE(before.desc != nullptr);

  const cogito::Error duplicate = registry.Register(EnabledTool("alpha.tool"));
  REQUIRE(duplicate.code == cogito::Errc::DuplicateKey);
  REQUIRE(registry.size() == 1U);
  REQUIRE(registry.Lookup("alpha.tool").desc == before.desc);

  cogito::ToolDescriptor invalid = EnabledTool("beta.tool");
  invalid.approval_required = true;
  invalid.idempotency = cogito::Idempotency::Unsafe;
  const cogito::Error rejected = registry.Register(std::move(invalid));
  REQUIRE(rejected.code == cogito::Errc::ToolContractViolation);
  REQUIRE(registry.size() == 1U);
  REQUIRE(registry.Lookup("beta.tool").kind == cogito::LookupKind::Absent);
}

TEST_CASE("RegisterFrom binds provider identity and commits a batch atomically",
          "[registry][provider]") {
  cogito::ToolRegistry registry;
  VectorProvider provider("batch_provider");
  provider.tools_.push_back(EnabledTool("alpha.tool"));
  provider.tools_.back().provider_id.clear();
  provider.tools_.push_back(EnabledTool("beta.tool"));
  provider.tools_.back().provider_id = "batch_provider";

  REQUIRE(registry.RegisterFrom(provider).ok());
  REQUIRE(registry.size() == 2U);
  REQUIRE(provider.describe_calls_ == 1U);
  REQUIRE(registry.Lookup("alpha.tool").desc->provider_id == "batch_provider");
  REQUIRE(registry.Lookup("beta.tool").desc->provider_id == "batch_provider");

  const cogito::ToolDescriptor* const alpha_before = registry.Lookup("alpha.tool").desc;
  VectorProvider mismatch("batch_provider");
  mismatch.tools_.push_back(EnabledTool("gamma.tool"));
  mismatch.tools_.back().provider_id = "other_provider";
  const cogito::Error mismatch_error = registry.RegisterFrom(mismatch);
  REQUIRE(mismatch_error.code == cogito::Errc::ToolContractViolation);
  REQUIRE(registry.size() == 2U);
  REQUIRE(registry.Lookup("gamma.tool").kind == cogito::LookupKind::Absent);
  REQUIRE(registry.Lookup("alpha.tool").desc == alpha_before);

  VectorProvider duplicate("batch_provider");
  duplicate.tools_.push_back(EnabledTool("gamma.tool"));
  duplicate.tools_.back().provider_id.clear();
  duplicate.tools_.push_back(EnabledTool("gamma.tool"));
  duplicate.tools_.back().provider_id.clear();
  const cogito::Error duplicate_error = registry.RegisterFrom(duplicate);
  REQUIRE(duplicate_error.code == cogito::Errc::DuplicateKey);
  REQUIRE(registry.size() == 2U);
  REQUIRE(registry.Lookup("gamma.tool").kind == cogito::LookupKind::Absent);

  VectorProvider existing("batch_provider");
  existing.tools_.push_back(EnabledTool("new.tool"));
  existing.tools_.back().provider_id.clear();
  existing.tools_.push_back(EnabledTool("alpha.tool"));
  existing.tools_.back().provider_id.clear();
  const cogito::Error existing_error = registry.RegisterFrom(existing);
  REQUIRE(existing_error.code == cogito::Errc::DuplicateKey);
  REQUIRE(registry.size() == 2U);
  REQUIRE(registry.Lookup("new.tool").kind == cogito::LookupKind::Absent);
}

TEST_CASE("RegisterFrom validates provider before Describe and propagates provider errors",
          "[registry][provider]") {
  for (const std::string& id : {std::string{}, std::string{"Bad"}, std::string{"bad.dot"},
                                std::string{"bad space"}, std::string(65U, 'a')}) {
    CAPTURE(id);
    cogito::ToolRegistry registry;
    VectorProvider provider(id);
    provider.tools_.push_back(EnabledTool("alpha.tool"));
    const cogito::Error result = registry.RegisterFrom(provider);
    REQUIRE(result.code == cogito::Errc::ToolContractViolation);
    REQUIRE(provider.describe_calls_ == 0U);
    REQUIRE(registry.empty());
  }

  cogito::ToolRegistry registry;
  VectorProvider null_provider("unused");
  null_provider.null_id_ = true;
  REQUIRE(registry.RegisterFrom(null_provider).code == cogito::Errc::ToolContractViolation);
  REQUIRE(null_provider.describe_calls_ == 0U);

  VectorProvider failed("batch_provider");
  failed.error_ = cogito::Error{cogito::Errc::ProviderError, {}, "provider failed"};
  const cogito::Error propagated = registry.RegisterFrom(failed);
  REQUIRE(propagated.code == cogito::Errc::ProviderError);
  REQUIRE(failed.describe_calls_ == 1U);
  REQUIRE(registry.empty());
}

TEST_CASE("RegisterFrom accepts provider identifier scanner boundaries",
          "[registry][provider][boundary]") {
  for (const std::string& id : {std::string{"a"}, std::string(64U, 'a')}) {
    CAPTURE(id.size());
    cogito::ToolRegistry registry;
    VectorProvider provider(id);
    provider.tools_.push_back(EnabledTool("boundary.tool"));
    provider.tools_.back().provider_id.clear();

    REQUIRE(registry.RegisterFrom(provider).ok());
    REQUIRE(provider.describe_calls_ == 1U);
    REQUIRE(registry.size() == 1U);
    REQUIRE(registry.Lookup("boundary.tool").desc->provider_id == id);
  }
}

TEST_CASE("Lookup keeps enabled forbidden and absent states distinct", "[registry][lookup]") {
  cogito::ToolRegistry registry;
  REQUIRE(registry.Register(EnabledTool("enabled.tool")).ok());
  REQUIRE(registry.Register(ForbiddenTool("forbidden.tool")).ok());

  const cogito::LookupResult enabled = registry.Lookup("enabled.tool");
  REQUIRE(enabled.kind == cogito::LookupKind::Enabled);
  REQUIRE(enabled.desc != nullptr);

  const cogito::LookupResult forbidden = registry.Lookup("forbidden.tool");
  REQUIRE(forbidden.kind == cogito::LookupKind::Forbidden);
  REQUIRE(forbidden.desc != nullptr);
  REQUIRE(forbidden.desc->forbidden_reason == "security_lockdown");

  const cogito::LookupResult absent = registry.Lookup("absent.tool");
  REQUIRE(absent.kind == cogito::LookupKind::Absent);
  REQUIRE(absent.desc == nullptr);
}

TEST_CASE("Freeze reproduces the canonical two-tool registry digest", "[registry][digest]") {
  const cogito::ToolDescriptor calc = CalcTool();
  const cogito::ToolDescriptor fs_delete = FsDeleteTool();
  const auto calc_schema_digest =
      cogito::ComputeToolSchemaDigest(calc.name, calc.input_schema, calc.output_schema);
  const auto fs_schema_digest = cogito::ComputeToolSchemaDigest(
      fs_delete.name, fs_delete.input_schema, fs_delete.output_schema);
  REQUIRE(calc_schema_digest.ok());
  REQUIRE(fs_schema_digest.ok());
  REQUIRE(calc_schema_digest.value().hex() ==
          "e1f97f4c393c55989d60303772331153d3aaced8fa68b2ef6b55ba4a1b7728ca");
  REQUIRE(fs_schema_digest.value().hex() ==
          "715b419cbee316e7ad17c7f8bfb0bc7059e3ac6ef5c0163856a9dce98ec1596d");

  cogito::ToolRegistry registry;
  REQUIRE(registry.Register(fs_delete).ok());
  REQUIRE(registry.Register(calc).ok());
  REQUIRE(registry.FindInputSchema("calc.add") == nullptr);
  REQUIRE(registry.ExportForModel(cogito::ExecutionMode::Default).empty());

  REQUIRE(registry.Freeze().ok());
  REQUIRE(registry.frozen());
  REQUIRE(registry.registry_digest().hex() ==
          "31c8148f2e21c391ec41e255d1d6a73749eb7ed1e28dd1264b712a6414bc2612");
  REQUIRE(registry.export_order_version() == "name-asc-v1");
  REQUIRE(registry.Lookup("calc.add").desc->grammar_coverage == cogito::GrammarCoverage::None);
  REQUIRE(registry.Lookup("fs.delete").desc->grammar_coverage == cogito::GrammarCoverage::None);
  REQUIRE(registry.FindInputSchema("calc.add") != nullptr);
  REQUIRE(registry.FindOutputSchema("calc.add") == nullptr);
  REQUIRE(registry.FindInputSchema("fs.delete") == nullptr);
  REQUIRE(registry.FindOutputSchema("fs.delete") == nullptr);

  const cogito::Digest frozen_digest = registry.registry_digest();
  const cogito::ToolDescriptor* const frozen_calc = registry.Lookup("calc.add").desc;
  REQUIRE(registry.Freeze().ok());
  REQUIRE(registry.registry_digest() == frozen_digest);
  REQUIRE(registry.Lookup("calc.add").desc == frozen_calc);
}

TEST_CASE("Freeze compiles non-null output schemas and exposes both immutable validators",
          "[registry][freeze][output]") {
  cogito::ToolDescriptor descriptor = EnabledTool("output.tool");
  descriptor.output_schema =
      Json{{"type", "object"},
           {"properties", {{"ok", Json{{"type", "boolean"}}}}},
           {"required", Json::array({"ok"})},
           {"additionalProperties", false}};

  cogito::ToolRegistry registry;
  REQUIRE(registry.Register(std::move(descriptor)).ok());
  REQUIRE(registry.Freeze().ok());
  REQUIRE(registry.FindInputSchema("output.tool") != nullptr);
  const cogito::CompiledSchema* const output = registry.FindOutputSchema("output.tool");
  REQUIRE(output != nullptr);
  REQUIRE(output->Validate(Json{{"ok", true}}).ok());
  REQUIRE(output->Validate(Json{{"ok", "yes"}}).code == cogito::Errc::SchemaViolation);
}

TEST_CASE("Freeze commits compiler coverage and skips forbidden schemas",
          "[registry][freeze][coverage]") {
  cogito::ToolDescriptor enabled = EnabledTool("enabled.coverage");
  enabled.grammar_coverage = cogito::GrammarCoverage::Full;

  cogito::ToolDescriptor forbidden = ForbiddenTool("forbidden.coverage");
  forbidden.input_schema = Json{{"futureKeyword", true}};
  forbidden.output_schema = Json{{"anotherFutureKeyword", true}};
  forbidden.grammar_coverage = cogito::GrammarCoverage::Full;

  cogito::ToolRegistry registry;
  REQUIRE(registry.Register(std::move(enabled)).ok());
  REQUIRE(registry.Register(std::move(forbidden)).ok());
  REQUIRE(registry.Freeze().ok());

  REQUIRE(registry.Lookup("enabled.coverage").desc->grammar_coverage ==
          cogito::GrammarCoverage::None);
  REQUIRE(registry.Lookup("forbidden.coverage").desc->grammar_coverage ==
          cogito::GrammarCoverage::None);
  REQUIRE(registry.FindInputSchema("enabled.coverage") != nullptr);
  REQUIRE(registry.FindInputSchema("forbidden.coverage") == nullptr);
  REQUIRE(registry.FindOutputSchema("forbidden.coverage") == nullptr);
}

TEST_CASE("Failed Freeze leaves tools coverage schemas and digest unchanged",
          "[registry][freeze][rollback]") {
  cogito::ToolDescriptor invalid = EnabledTool("invalid.tool");
  invalid.input_schema = Json{{"type", "object"}, {"futureKeyword", true}};
  invalid.grammar_coverage = cogito::GrammarCoverage::Full;

  cogito::ToolRegistry registry;
  REQUIRE(registry.Register(std::move(invalid)).ok());
  const cogito::ToolDescriptor* const before = registry.Lookup("invalid.tool").desc;
  REQUIRE(before != nullptr);
  REQUIRE(before->grammar_coverage == cogito::GrammarCoverage::Full);

  const cogito::Error failed = registry.Freeze();
  REQUIRE(failed.code == cogito::Errc::SchemaCompileFailed);
  REQUIRE_FALSE(registry.frozen());
  REQUIRE(registry.registry_digest().is_zero());
  REQUIRE(registry.Lookup("invalid.tool").desc == before);
  REQUIRE(registry.Lookup("invalid.tool").desc->grammar_coverage ==
          cogito::GrammarCoverage::Full);
  REQUIRE(registry.FindInputSchema("invalid.tool") == nullptr);
  REQUIRE(registry.FindOutputSchema("invalid.tool") == nullptr);
  REQUIRE(registry.Freeze().code == cogito::Errc::SchemaCompileFailed);

  cogito::ToolDescriptor invalid_output = EnabledTool("output.tool");
  invalid_output.output_schema = Json{{"type", "object"}, {"futureKeyword", true}};
  cogito::ToolRegistry output_registry;
  REQUIRE(output_registry.Register(std::move(invalid_output)).ok());
  REQUIRE(output_registry.Freeze().code == cogito::Errc::SchemaCompileFailed);
  REQUIRE_FALSE(output_registry.frozen());
  REQUIRE(output_registry.FindInputSchema("output.tool") == nullptr);

  cogito::ToolRegistry corrected;
  REQUIRE(corrected.Register(EnabledTool("invalid.tool")).ok());
  REQUIRE(corrected.Freeze().ok());
  REQUIRE(corrected.frozen());
}

TEST_CASE("Digest failure during Freeze also preserves the complete pre-freeze state",
          "[registry][freeze][rollback]") {
  cogito::ToolDescriptor descriptor = ForbiddenTool("invalid.digest");
  descriptor.forbidden_reason = std::string(1U, static_cast<char>(0xFF));
  descriptor.grammar_coverage = cogito::GrammarCoverage::Full;

  cogito::ToolRegistry registry;
  REQUIRE(registry.Register(std::move(descriptor)).ok());
  const cogito::ToolDescriptor* const before = registry.Lookup("invalid.digest").desc;
  const cogito::Error error = registry.Freeze();
  REQUIRE(error.code == cogito::Errc::NotUtf8);
  REQUIRE_FALSE(registry.frozen());
  REQUIRE(registry.registry_digest().is_zero());
  REQUIRE(registry.Lookup("invalid.digest").desc == before);
  REQUIRE(registry.Lookup("invalid.digest").desc->grammar_coverage ==
          cogito::GrammarCoverage::Full);
}

TEST_CASE("Frozen Registry rejects every registration path without invoking providers",
          "[registry][freeze][sealed]") {
  cogito::ToolRegistry registry;
  REQUIRE(registry.Register(EnabledTool("alpha.tool")).ok());
  REQUIRE(registry.Freeze().ok());
  const std::size_t size = registry.size();
  const cogito::Digest digest = registry.registry_digest();

  REQUIRE(registry.Register(EnabledTool("beta.tool")).code == cogito::Errc::TurnSealed);
  VectorProvider provider("batch_provider");
  provider.tools_.push_back(EnabledTool("gamma.tool"));
  REQUIRE(registry.RegisterFrom(provider).code == cogito::Errc::TurnSealed);
  REQUIRE(provider.describe_calls_ == 0U);
  REQUIRE(registry.size() == size);
  REQUIRE(registry.registry_digest() == digest);
  REQUIRE(registry.Lookup("beta.tool").kind == cogito::LookupKind::Absent);
  REQUIRE(registry.Lookup("gamma.tool").kind == cogito::LookupKind::Absent);
}

TEST_CASE("ValidateArguments preserves gate error classes and never invokes handlers",
          "[registry][validate]") {
  std::size_t handler_calls = 0U;
  cogito::ToolDescriptor descriptor = EnabledTool("pattern.tool", cogito::Effect::None,
                                                   &handler_calls);
  descriptor.input_schema =
      Json{{"type", "object"},
           {"properties",
            {{"value",
              Json{{"type", "string"},
                   {"maxLength", cogito::kMaxMatchStringBytes},
                   {"pattern", "^[a-z]*$"}}}}},
           {"required", Json::array({"value"})},
           {"additionalProperties", false}};

  cogito::ToolRegistry pre_freeze;
  REQUIRE(pre_freeze.Register(descriptor).ok());
  REQUIRE(pre_freeze.ValidateArguments("pattern.tool", Json{{"value", "abc"}}).code ==
          cogito::Errc::Internal);

  cogito::ToolRegistry registry;
  REQUIRE(registry.Register(std::move(descriptor)).ok());
  REQUIRE(registry.Register(ForbiddenTool("blocked.tool")).ok());
  REQUIRE(registry.Freeze().ok());

  REQUIRE(registry.ValidateArguments("pattern.tool", Json{{"value", "abc"}}).ok());
  const cogito::Error violation =
      registry.ValidateArguments("pattern.tool", Json{{"value", "ABC"}});
  REQUIRE(violation.code == cogito::Errc::SchemaViolation);
  REQUIRE(violation.reason_code == cogito::reason::kSchemaViolation);

  const cogito::Error absent = registry.ValidateArguments("absent.tool", Json::object());
  REQUIRE(absent.code == cogito::Errc::NotRegistered);
  REQUIRE(absent.reason_code == cogito::reason::kToolNotRegistered);

  const cogito::Error forbidden = registry.ValidateArguments("blocked.tool", Json::object());
  REQUIRE(forbidden.code == cogito::Errc::Forbidden);
  REQUIRE(forbidden.reason_code == cogito::reason::kToolForbidden);

  const std::string long_input(cogito::kMaxMatchStringBytes, 'a');
  const cogito::Error exhausted =
      registry.ValidateArguments("pattern.tool", Json{{"value", long_input}});
  REQUIRE(exhausted.code == cogito::Errc::PatternBudgetExhausted);
  REQUIRE(exhausted.reason_code == cogito::reason::kPatternTimeout);
  REQUIRE(handler_calls == 0U);
}

TEST_CASE("ExportForModel is frozen ordered filtered and handler free by type",
          "[registry][export]") {
  cogito::ToolRegistry registry;
  REQUIRE(registry.Register(EnabledTool("c.destroy", cogito::Effect::Destructive)).ok());
  REQUIRE(registry.Register(EnabledTool("a.none", cogito::Effect::None)).ok());
  REQUIRE(registry.Register(ForbiddenTool("aa.hidden")).ok());
  REQUIRE(registry.Register(EnabledTool("b.write", cogito::Effect::Write)).ok());

  REQUIRE(registry.ExportForModel(cogito::ExecutionMode::Default).empty());
  REQUIRE(registry.Freeze().ok());

  REQUIRE(ExportedNames(registry, cogito::ExecutionMode::ReadOnly) ==
          std::vector<std::string>{"a.none"});
  REQUIRE(ExportedNames(registry, cogito::ExecutionMode::Plan) ==
          std::vector<std::string>{"a.none"});
  REQUIRE(ExportedNames(registry, cogito::ExecutionMode::Edit) ==
          std::vector<std::string>{"a.none", "b.write"});
  REQUIRE(ExportedNames(registry, cogito::ExecutionMode::Default) ==
          std::vector<std::string>{"a.none", "b.write", "c.destroy"});
  REQUIRE(ExportedNames(registry, static_cast<cogito::ExecutionMode>(255U)) ==
          std::vector<std::string>{"a.none"});

  const std::vector<cogito::ModelToolDeclaration> declarations =
      registry.ExportForModel(cogito::ExecutionMode::Default);
  REQUIRE(declarations.front().parameters == registry.Lookup("a.none").desc->input_schema);
}

TEST_CASE("Moving a frozen Registry transfers authoritative state", "[registry][lifetime]") {
  cogito::ToolRegistry source;
  REQUIRE(source.Register(EnabledTool("move.tool")).ok());
  REQUIRE(source.Freeze().ok());
  const cogito::Digest digest = source.registry_digest();

  cogito::ToolRegistry moved(std::move(source));
  REQUIRE(moved.frozen());
  REQUIRE(moved.registry_digest() == digest);
  REQUIRE(moved.Lookup("move.tool").kind == cogito::LookupKind::Enabled);
  REQUIRE(moved.FindInputSchema("move.tool") != nullptr);

  cogito::ToolRegistry assigned;
  assigned = std::move(moved);
  REQUIRE(assigned.frozen());
  REQUIRE(assigned.registry_digest() == digest);
  REQUIRE(assigned.Lookup("move.tool").kind == cogito::LookupKind::Enabled);
}
