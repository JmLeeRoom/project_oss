// SPDX-License-Identifier: Apache-2.0

#include "cogito/registry.hpp"

#include <set>
#include <string_view>
#include <utility>

namespace cogito {
namespace {

constexpr std::size_t kMaxProviderIdBytes = 64U;

Error RegistryError(Errc code, std::string message) {
  return Error{code, {}, std::move(message)};
}

Error InternalRegistryError() {
  return RegistryError(Errc::Internal, "registry operation failed");
}

bool IsLowerAscii(char ch) noexcept { return ch >= 'a' && ch <= 'z'; }

bool IsDigitAscii(char ch) noexcept { return ch >= '0' && ch <= '9'; }

bool IsValidProviderId(std::string_view id) noexcept {
  if (id.empty() || id.size() > kMaxProviderIdBytes) {
    return false;
  }
  for (const char ch : id) {
    if (!IsLowerAscii(ch) && !IsDigitAscii(ch) && ch != '_' && ch != '-') {
      return false;
    }
  }
  return true;
}

Result<std::string> ReadProviderId(const ToolProvider& provider) {
  const char* const raw = provider.provider_id();
  if (raw == nullptr) {
    return RegistryError(Errc::ToolContractViolation, "provider_id is null");
  }

  std::size_t size = 0U;
  while (size <= kMaxProviderIdBytes && raw[size] != '\0') {
    ++size;
  }
  if (size == 0U || size > kMaxProviderIdBytes) {
    return RegistryError(Errc::ToolContractViolation,
                         "provider_id is outside the approved byte limit");
  }

  const std::string_view view(raw, size);
  if (!IsValidProviderId(view)) {
    return RegistryError(Errc::ToolContractViolation,
                         "provider_id is outside the approved grammar");
  }
  return std::string(view);
}

Error CompileFailure(const Error& error) {
  if (error.code == Errc::SchemaCompileFailed) {
    return error;
  }
  return Error{Errc::SchemaCompileFailed, {}, "schema compile failed", error.detail};
}

}  // namespace

const char* ToString(LookupKind kind) noexcept {
  switch (kind) {
    case LookupKind::Absent:
      return "absent";
    case LookupKind::Enabled:
      return "enabled";
    case LookupKind::Forbidden:
      return "forbidden";
  }
  return "unknown";
}

ToolRegistry::ToolRegistry() = default;
ToolRegistry::~ToolRegistry() = default;
ToolRegistry::ToolRegistry(ToolRegistry&&) noexcept = default;
ToolRegistry& ToolRegistry::operator=(ToolRegistry&&) noexcept = default;

Error ToolRegistry::Register(ToolDescriptor descriptor) {
  try {
    if (frozen_) {
      return RegistryError(Errc::TurnSealed, "registry is frozen");
    }
    const Error contract = ValidateToolContract(descriptor);
    if (contract) {
      return contract;
    }
    if (tools_.find(descriptor.name) != tools_.end()) {
      return RegistryError(Errc::DuplicateKey, "tool name is already registered");
    }

    const std::string name = descriptor.name;
    tools_.emplace(name, std::move(descriptor));
    return Error::Ok();
  } catch (...) {
    return InternalRegistryError();
  }
}

Error ToolRegistry::RegisterFrom(ToolProvider& provider) {
  try {
    if (frozen_) {
      return RegistryError(Errc::TurnSealed, "registry is frozen");
    }

    auto provider_id_result = ReadProviderId(provider);
    if (!provider_id_result.ok()) {
      return provider_id_result.error();
    }
    const std::string provider_id = std::move(provider_id_result).take();

    auto described = provider.Describe();
    if (!described.ok()) {
      return described.error();
    }
    std::vector<ToolDescriptor> batch = std::move(described).take();
    std::set<std::string> batch_names;
    for (ToolDescriptor& descriptor : batch) {
      if (descriptor.provider_id.empty()) {
        descriptor.provider_id = provider_id;
      } else if (descriptor.provider_id != provider_id) {
        return RegistryError(Errc::ToolContractViolation,
                             "descriptor provider_id does not match its provider");
      }

      const Error contract = ValidateToolContract(descriptor);
      if (contract) {
        return contract;
      }
      if (tools_.find(descriptor.name) != tools_.end() ||
          !batch_names.insert(descriptor.name).second) {
        return RegistryError(Errc::DuplicateKey, "provider batch contains a duplicate tool");
      }
    }

    std::map<std::string, ToolDescriptor> staged_tools = tools_;
    for (ToolDescriptor& descriptor : batch) {
      const std::string name = descriptor.name;
      staged_tools.emplace(name, std::move(descriptor));
    }
    tools_.swap(staged_tools);
    return Error::Ok();
  } catch (...) {
    return InternalRegistryError();
  }
}

Error ToolRegistry::Freeze() {
  try {
    if (frozen_) {
      return Error::Ok();
    }

    std::map<std::string, ToolDescriptor> staged_tools = tools_;
    std::map<std::string, std::unique_ptr<CompiledSchema>> staged_inputs;
    std::map<std::string, std::unique_ptr<CompiledSchema>> staged_outputs;

    for (auto& entry : staged_tools) {
      ToolDescriptor& descriptor = entry.second;
      const Error contract = ValidateToolContract(descriptor);
      if (contract) {
        return contract;
      }

      if (descriptor.status == ToolStatus::Forbidden) {
        descriptor.grammar_coverage = GrammarCoverage::None;
        continue;
      }

      auto input = SchemaCompiler::Compile(descriptor.input_schema);
      if (!input.ok()) {
        return CompileFailure(input.error());
      }
      descriptor.grammar_coverage = input.value()->audit().coverage;
      staged_inputs.emplace(descriptor.name, std::move(input).take());

      if (!descriptor.output_schema.is_null()) {
        auto output = SchemaCompiler::Compile(descriptor.output_schema);
        if (!output.ok()) {
          return CompileFailure(output.error());
        }
        staged_outputs.emplace(descriptor.name, std::move(output).take());
      }
    }

    std::vector<ToolProjectionDto> projections;
    projections.reserve(staged_tools.size());
    for (const auto& entry : staged_tools) {
      const ToolDescriptor& descriptor = entry.second;
      auto schema_digest = ComputeToolSchemaDigest(
          descriptor.name, descriptor.input_schema, descriptor.output_schema);
      if (!schema_digest.ok()) {
        return schema_digest.error();
      }

      ToolProjectionDto projection;
      projection.name = descriptor.name;
      projection.status = ToString(descriptor.status);
      projection.forbidden_reason = descriptor.forbidden_reason;
      projection.toolschema_digest = schema_digest.value();
      projection.grammar_coverage = ToString(descriptor.grammar_coverage);
      projection.effect = ToString(descriptor.effect);
      projection.risk = ToString(descriptor.risk);
      projection.idempotency = ToString(descriptor.idempotency);
      projection.approval_required = descriptor.approval_required ? 1U : 0U;
      projection.timeout_ms = static_cast<std::uint64_t>(descriptor.timeout_ms);
      projection.max_output_bytes = static_cast<std::uint64_t>(descriptor.max_output_bytes);
      projection.provider_id = descriptor.provider_id;
      projection.invoker_id = descriptor.invoker_id;
      projections.push_back(std::move(projection));
    }

    auto computed_digest = ComputeRegistryDigest(std::move(projections));
    if (!computed_digest.ok()) {
      return computed_digest.error();
    }

    tools_.swap(staged_tools);
    input_schemas_.swap(staged_inputs);
    output_schemas_.swap(staged_outputs);
    digest_ = computed_digest.value();
    frozen_ = true;
    return Error::Ok();
  } catch (...) {
    return InternalRegistryError();
  }
}

LookupResult ToolRegistry::Lookup(const std::string& name) const noexcept {
  const auto found = tools_.find(name);
  if (found == tools_.end()) {
    return LookupResult{LookupKind::Absent, nullptr};
  }
  if (found->second.status == ToolStatus::Enabled) {
    return LookupResult{LookupKind::Enabled, &found->second};
  }
  if (found->second.status == ToolStatus::Forbidden) {
    return LookupResult{LookupKind::Forbidden, &found->second};
  }
  return LookupResult{LookupKind::Absent, nullptr};
}

const CompiledSchema* ToolRegistry::FindInputSchema(const std::string& tool) const noexcept {
  if (!frozen_) {
    return nullptr;
  }
  const auto found = input_schemas_.find(tool);
  return found == input_schemas_.end() ? nullptr : found->second.get();
}

const CompiledSchema* ToolRegistry::FindOutputSchema(const std::string& tool) const noexcept {
  if (!frozen_) {
    return nullptr;
  }
  const auto found = output_schemas_.find(tool);
  return found == output_schemas_.end() ? nullptr : found->second.get();
}

Error ToolRegistry::ValidateArguments(const std::string& tool, const ccj::Json& args) const {
  if (!frozen_) {
    return RegistryError(Errc::Internal, "registry is not frozen");
  }

  const LookupResult lookup = Lookup(tool);
  if (lookup.kind == LookupKind::Absent) {
    return Error{Errc::NotRegistered, reason::kToolNotRegistered, "tool is not registered"};
  }
  if (lookup.kind == LookupKind::Forbidden) {
    return Error{Errc::Forbidden, reason::kToolForbidden, "tool is forbidden"};
  }

  const CompiledSchema* const schema = FindInputSchema(tool);
  if (schema == nullptr) {
    return InternalRegistryError();
  }
  return schema->Validate(args);
}

std::vector<ModelToolDeclaration> ToolRegistry::ExportForModel(ExecutionMode mode) const {
  if (!frozen_) {
    return {};
  }

  const Effect max_effect = ModeToMaxEffect(mode);
  std::vector<ModelToolDeclaration> declarations;
  for (const auto& entry : tools_) {
    const ToolDescriptor& descriptor = entry.second;
    if (descriptor.status != ToolStatus::Enabled ||
        static_cast<std::uint8_t>(descriptor.effect) >
            static_cast<std::uint8_t>(max_effect)) {
      continue;
    }
    declarations.push_back(
        ModelToolDeclaration{descriptor.name, descriptor.description, descriptor.input_schema});
  }
  return declarations;
}

}  // namespace cogito
