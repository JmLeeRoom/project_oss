// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 도구 실행자 (ToolInvoker) 구현

#include "cogito/invoker.hpp"

#include <exception>
#include <string>
#include <utility>

#include "cogito/canonical_json.hpp"
#include "cogito/clock.hpp"
#include "cogito/digest.hpp"
#include "cogito/ids.hpp"
#include "cogito/inference.hpp"
#include "cogito/permit.hpp"
#include "cogito/registry.hpp"
#include "cogito/result.hpp"
#include "cogito/tool.hpp"
#include "cogito/tool_schema.hpp"

namespace cogito {
namespace {

std::string DumpJson(const ccj::Json& j) {
  auto res = ccj::Serialize(j);
  if (res.ok()) {
    return res.value();
  }
  return j.dump();
}

}  // namespace

ccj::Json MakeInvalidToolResult(const std::string& tool_name,
                                const std::string& reason_code,
                                const std::string& detail_masked) {
  return ccj::Json{
      {"status", "error"},
      {"tool", tool_name},
      {"reason_code", reason_code},
      {"detail", detail_masked}
  };
}

ToolResult ToolInvoker::Invoke(ExecutionPermit&& permit,
                               const ccj::Json& arguments,
                               const ToolCallContext& ctx) {
  ToolResult result;
  result.attempt_count = 1;

  std::int64_t now_ns = clock_.MonotonicNs();
  result.started_ns = now_ns;

  // 1. Permit Validation (valid, non-consumed, unexpired, tool match, scope match)
  if (!permit.valid()) {
    result.status = ToolResultStatus::Error;
    result.error_code = reason::kInvalidFsmState;
    result.error_message = "ExecutionPermit is invalid or already consumed";
    result.finished_ns = clock_.MonotonicNs();
    result.elapsed_us = (result.finished_ns >= result.started_ns)
                            ? ((result.finished_ns - result.started_ns) / 1000)
                            : 0;
    result.content = MakeInvalidToolResult(permit.tool_name(), result.error_code,
                                          result.error_message);
    return result;
  }

  LookupResult lookup = reg_.Lookup(permit.tool_name());
  if (lookup.kind != LookupKind::Enabled || lookup.desc == nullptr ||
      !lookup.desc->has_handler()) {
    result.status = ToolResultStatus::Error;
    result.error_code = (lookup.kind == LookupKind::Forbidden)
                            ? reason::kToolForbidden
                            : reason::kToolNotRegistered;
    result.error_message = (lookup.kind == LookupKind::Forbidden)
                               ? lookup.desc->forbidden_reason
                               : "Tool is not registered or has no handler";
    result.finished_ns = clock_.MonotonicNs();
    result.elapsed_us = (result.finished_ns >= result.started_ns)
                            ? ((result.finished_ns - result.started_ns) / 1000)
                            : 0;
    result.content = MakeInvalidToolResult(permit.tool_name(), result.error_code,
                                          result.error_message);
    return result;
  }

  const ToolDescriptor& td = *lookup.desc;

  Error usable_err =
      permit.CheckUsable(td.name, permit.permit_scope_digest(), now_ns);
  if (!usable_err.ok()) {
    result.status = ToolResultStatus::Error;
    result.error_code = usable_err.reason_code.empty()
                            ? reason::kApprovalScopeMismatch
                            : usable_err.reason_code;
    result.error_message = usable_err.message;
    result.finished_ns = clock_.MonotonicNs();
    result.elapsed_us = (result.finished_ns >= result.started_ns)
                            ? ((result.finished_ns - result.started_ns) / 1000)
                            : 0;
    result.content =
        MakeInvalidToolResult(td.name, result.error_code, result.error_message);
    return result;
  }

  // Pre-invocation cancellation check
  if (ctx.cancel != nullptr && ctx.cancel->IsCancelled()) {
    result.status = (td.effect == Effect::None)
                        ? ToolResultStatus::Cancelled
                        : ToolResultStatus::Indeterminate;
    result.error_code = "cancelled";
    result.error_message = "Tool execution was cancelled before invocation";
    result.finished_ns = clock_.MonotonicNs();
    result.elapsed_us = (result.finished_ns >= result.started_ns)
                            ? ((result.finished_ns - result.started_ns) / 1000)
                            : 0;
    result.content =
        MakeInvalidToolResult(td.name, result.error_code, result.error_message);
    return result;
  }

  // Pre-invocation deadline check
  if ((ctx.deadline_ns > 0 && now_ns >= ctx.deadline_ns) ||
      permit.IsExpired(now_ns)) {
    result.status = (td.effect == Effect::None)
                        ? ToolResultStatus::Timeout
                        : ToolResultStatus::Indeterminate;
    result.error_code = reason::kBudgetDeadline;
    result.error_message = "Tool deadline exceeded before invocation";
    result.finished_ns = clock_.MonotonicNs();
    result.elapsed_us = (result.finished_ns >= result.started_ns)
                            ? ((result.finished_ns - result.started_ns) / 1000)
                            : 0;
    result.content =
        MakeInvalidToolResult(td.name, result.error_code, result.error_message);
    return result;
  }

  // 2. Consume Permit — EXACTLY ONCE before handler invocation
  permit.Consume();

  // 3. Handler invocation with exception isolation (G0-09 Rule 3)
  try {
    result = td.handler_(arguments, ctx);
  } catch (const std::exception& ex) {
    result.status = ToolResultStatus::Error;
    result.error_code = "handler_exception";
    result.error_message = ex.what();
    result.content = MakeInvalidToolResult(td.name, result.error_code,
                                          result.error_message);
  } catch (...) {
    result.status = ToolResultStatus::Error;
    result.error_code = "handler_exception";
    result.error_message = "Unknown exception thrown by tool handler";
    result.content = MakeInvalidToolResult(td.name, result.error_code,
                                          result.error_message);
  }

  // 4. Update timing metadata
  result.started_ns = now_ns;
  result.finished_ns = clock_.MonotonicNs();
  result.elapsed_us = (result.finished_ns >= result.started_ns)
                          ? ((result.finished_ns - result.started_ns) / 1000)
                          : 0;
  result.attempt_count = 1;

  // 5. Post-invocation Cancellation and Timeout Classification (ADR-0001 R3)
  if (ctx.cancel != nullptr && ctx.cancel->IsCancelled()) {
    result.status = (td.effect == Effect::None)
                        ? ToolResultStatus::Cancelled
                        : ToolResultStatus::Indeterminate;
    if (result.error_code.empty()) {
      result.error_code = "cancelled";
      result.error_message = "Tool execution was cancelled";
    }
  } else if (result.status == ToolResultStatus::Timeout ||
             (ctx.deadline_ns > 0 && result.finished_ns >= ctx.deadline_ns) ||
             permit.IsExpired(result.finished_ns)) {
    result.status = (td.effect == Effect::None)
                        ? ToolResultStatus::Timeout
                        : ToolResultStatus::Indeterminate;
    if (result.error_code.empty()) {
      result.error_code = reason::kBudgetDeadline;
      result.error_message = "Tool execution timed out or deadline exceeded";
    }
  }

  if (td.effect != Effect::None &&
      (result.status == ToolResultStatus::Cancelled ||
       result.status == ToolResultStatus::Timeout)) {
    result.status = ToolResultStatus::Indeterminate;
  }

  // 6. Result Validation (G0-27)
  // Output size limit -> JSON validity -> Output schema validation
  if (result.status == ToolResultStatus::Ok) {
    std::string dumped = DumpJson(result.content);
    result.output_bytes = dumped.size();

    if (result.output_bytes > td.max_output_bytes) {
      result.status = ToolResultStatus::Error;
      result.error_code = reason::kInputTooLarge;
      result.error_message = "Tool output exceeded max_output_bytes limit";
      result.truncated = true;
      result.content = MakeInvalidToolResult(
          td.name, reason::kInputTooLarge, "Output size exceeded limit");
    } else if (result.content.is_discarded()) {
      result.status = ToolResultStatus::Error;
      result.error_code = reason::kSchemaViolation;
      result.error_message = "Tool output JSON is discarded or invalid";
      result.content = MakeInvalidToolResult(
          td.name, reason::kSchemaViolation, "Invalid JSON output");
    } else if (!td.output_schema.is_null() && !td.output_schema.empty()) {
      const CompiledSchema* schema = reg_.FindOutputSchema(td.name);
      if (schema != nullptr) {
        Error s_err = schema->Validate(result.content);
        if (!s_err.ok()) {
          result.status = ToolResultStatus::Error;
          result.error_code = reason::kSchemaViolation;
          result.error_message = s_err.message;
          result.content = MakeInvalidToolResult(
              td.name, reason::kSchemaViolation, s_err.message);
        }
      }
    }
  } else {
    if (!result.content.is_null()) {
      result.output_bytes = DumpJson(result.content).size();
    }
  }

  return result;
}

}  // namespace cogito
