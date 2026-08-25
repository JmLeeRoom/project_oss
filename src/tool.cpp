// SPDX-License-Identifier: Apache-2.0

#include "cogito/tool.hpp"

#include <string_view>

namespace cogito {
namespace {

constexpr std::size_t kMaxToolNameBytes = 128U;
constexpr std::int32_t kMaxTimeoutMs = 3'600'000;
constexpr std::size_t kMaxOutputBytes = 10'485'760U;
constexpr std::size_t kMaxAttributionIdBytes = 64U;

Error ContractViolation(std::string detail) {
  return Error{Errc::ToolContractViolation, {}, "tool contract violation", std::move(detail)};
}

bool IsLowerAscii(char ch) noexcept { return ch >= 'a' && ch <= 'z'; }

bool IsDigitAscii(char ch) noexcept { return ch >= '0' && ch <= '9'; }

bool IsValidToolName(std::string_view name) noexcept {
  if (name.empty() || name.size() > kMaxToolNameBytes || !IsLowerAscii(name.front())) {
    return false;
  }

  std::size_t dot_count = 0U;
  bool segment_start = false;
  for (std::size_t index = 1U; index < name.size(); ++index) {
    const char ch = name[index];
    if (ch == '.') {
      ++dot_count;
      if (dot_count > 4U || segment_start || index + 1U == name.size()) {
        return false;
      }
      segment_start = true;
      continue;
    }
    if (segment_start) {
      if (!IsLowerAscii(ch)) {
        return false;
      }
      segment_start = false;
      continue;
    }
    if (!IsLowerAscii(ch) && !IsDigitAscii(ch) && ch != '_') {
      return false;
    }
  }
  return !segment_start;
}

bool IsValidAttributionId(std::string_view id) noexcept {
  if (id.empty() || id.size() > kMaxAttributionIdBytes) {
    return false;
  }
  for (const char ch : id) {
    if (!IsLowerAscii(ch) && !IsDigitAscii(ch) && ch != '_' && ch != '-') {
      return false;
    }
  }
  return true;
}

bool IsValid(Effect value) noexcept {
  switch (value) {
    case Effect::None:
    case Effect::Write:
    case Effect::Destructive:
      return true;
  }
  return false;
}

bool IsValid(Risk value) noexcept {
  switch (value) {
    case Risk::Low:
    case Risk::Medium:
    case Risk::High:
    case Risk::Critical:
      return true;
  }
  return false;
}

bool IsValid(Idempotency value) noexcept {
  switch (value) {
    case Idempotency::Safe:
    case Idempotency::Conditional:
    case Idempotency::Unsafe:
      return true;
  }
  return false;
}

bool IsValid(ToolStatus value) noexcept {
  switch (value) {
    case ToolStatus::Enabled:
    case ToolStatus::Forbidden:
      return true;
  }
  return false;
}

bool MatrixAllows(const ToolDescriptor& descriptor) noexcept {
  switch (descriptor.effect) {
    case Effect::None:
      return descriptor.idempotency == Idempotency::Safe ||
             descriptor.idempotency == Idempotency::Conditional;
    case Effect::Write:
      return descriptor.risk != Risk::Low && descriptor.approval_required &&
             (descriptor.idempotency == Idempotency::Conditional ||
              descriptor.idempotency == Idempotency::Unsafe);
    case Effect::Destructive:
      return (descriptor.risk == Risk::High || descriptor.risk == Risk::Critical) &&
             descriptor.approval_required && descriptor.idempotency == Idempotency::Unsafe;
  }
  return false;
}

}  // namespace

const char* ToString(Effect value) noexcept {
  switch (value) {
    case Effect::None:
      return "none";
    case Effect::Write:
      return "write";
    case Effect::Destructive:
      return "destructive";
  }
  return "unknown";
}

const char* ToString(Risk value) noexcept {
  switch (value) {
    case Risk::Low:
      return "low";
    case Risk::Medium:
      return "medium";
    case Risk::High:
      return "high";
    case Risk::Critical:
      return "critical";
  }
  return "unknown";
}

const char* ToString(Idempotency value) noexcept {
  switch (value) {
    case Idempotency::Safe:
      return "safe";
    case Idempotency::Conditional:
      return "conditional";
    case Idempotency::Unsafe:
      return "unsafe";
  }
  return "unknown";
}

const char* ToString(ToolStatus value) noexcept {
  switch (value) {
    case ToolStatus::Enabled:
      return "enabled";
    case ToolStatus::Forbidden:
      return "forbidden";
  }
  return "unknown";
}

const char* ToString(ToolResultStatus value) noexcept {
  switch (value) {
    case ToolResultStatus::Ok:
      return "ok";
    case ToolResultStatus::Error:
      return "error";
    case ToolResultStatus::Timeout:
      return "timeout";
    case ToolResultStatus::Cancelled:
      return "cancelled";
    case ToolResultStatus::Indeterminate:
      return "indeterminate";
  }
  return "unknown";
}

Error ValidateToolContract(const ToolDescriptor& descriptor) {
  if (!IsValid(descriptor.effect) || !IsValid(descriptor.risk) ||
      !IsValid(descriptor.idempotency) || !IsValid(descriptor.status)) {
    return ContractViolation("descriptor contains an invalid enum value");
  }
  if (!IsValidToolName(descriptor.name)) {
    return ContractViolation("tool name is outside the approved grammar or byte limit");
  }
  if (descriptor.timeout_ms < 1 || descriptor.timeout_ms > kMaxTimeoutMs) {
    return ContractViolation("timeout_ms is outside the approved range");
  }
  if (descriptor.max_output_bytes < 1U || descriptor.max_output_bytes > kMaxOutputBytes) {
    return ContractViolation("max_output_bytes is outside the approved range");
  }
  if (!IsValidAttributionId(descriptor.provider_id) ||
      !IsValidAttributionId(descriptor.invoker_id)) {
    return ContractViolation("provider_id or invoker_id is outside the approved grammar");
  }
  if (!MatrixAllows(descriptor)) {
    return ContractViolation("effect, risk, approval, and idempotency disagree");
  }

  if (descriptor.status == ToolStatus::Enabled) {
    if (!descriptor.input_schema.is_object()) {
      return ContractViolation("enabled tool input_schema must be an object");
    }
    if (!descriptor.has_handler()) {
      return ContractViolation("enabled tool must have a handler");
    }
    if (!descriptor.forbidden_reason.empty()) {
      return ContractViolation("enabled tool forbidden_reason must be empty");
    }
  } else {
    if (descriptor.forbidden_reason.empty()) {
      return ContractViolation("forbidden tool must retain a reason");
    }
    if (descriptor.has_handler()) {
      return ContractViolation("forbidden tool cannot retain a handler");
    }
  }

  return Error::Ok();
}

}  // namespace cogito
