// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 권한 관문 (PermissionGate) 구현
//
// 규범 근거 : Cogito++_구현명세서.md §4-8(:840-868), §6-2(:1480-1615), §6-2-a(:1617-1645),
//             §8-4 [S-2], §2 레이아웃(:81), §3 불변식 1·3·5·6
// G0 결정   : G0-04 (indeterminate lockdown), G0-05 (operation digest), G0-25 (GateInput),
//             G0-30 (FindUsable), G0-31 (gate_reentry_count 상한)

#include "cogito/permission_gate.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace cogito {
namespace {

bool IsValidUtf8(std::string_view text) noexcept {
  const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
  std::size_t i = 0;
  while (i < text.size()) {
    const unsigned char lead = bytes[i];
    if (lead <= 0x7FU) {
      ++i;
      continue;
    }

    std::size_t continuation_count = 0;
    std::uint32_t code_point = 0;
    if (lead >= 0xC2U && lead <= 0xDFU) {
      continuation_count = 1;
      code_point = lead & 0x1FU;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
      continuation_count = 2;
      code_point = lead & 0x0FU;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
      continuation_count = 3;
      code_point = lead & 0x07U;
    } else {
      return false;
    }

    if (continuation_count > text.size() - i - 1U) {
      return false;
    }
    for (std::size_t j = 1; j <= continuation_count; ++j) {
      const unsigned char continuation = bytes[i + j];
      if ((continuation & 0xC0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | (continuation & 0x3FU);
    }
    if ((continuation_count == 1U && code_point < 0x80U) ||
        (continuation_count == 2U && code_point < 0x800U) ||
        (continuation_count == 3U && code_point < 0x10000U) ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU) ||
        code_point > 0x10FFFFU) {
      return false;
    }
    i += continuation_count + 1U;
  }
  return true;
}

int JsonDepth(const ccj::Json& j) {
  if (j.is_object()) {
    int max_child = 0;
    for (auto it = j.begin(); it != j.end(); ++it) {
      max_child = std::max(max_child, JsonDepth(it.value()));
    }
    return 1 + max_child;
  }
  if (j.is_array()) {
    int max_child = 0;
    for (const auto& elem : j) {
      max_child = std::max(max_child, JsonDepth(elem));
    }
    return 1 + max_child;
  }
  return 0;
}

std::int64_t SaturatingAdd(std::int64_t base, std::int64_t positive_delta) noexcept {
  if (positive_delta <= 0) {
    return base;
  }
  constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
  return base > kMax - positive_delta ? kMax : base + positive_delta;
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

}  // namespace

Verdict PermissionGate::Evaluate(const ActionRequest& a, const GateInput& in) const {
  Verdict v;
  v.action_digest = in.action_digest;
  v.policy_digest = policy_.policy_digest();
  v.registry_digest = registry_.registry_digest();
  v.evaluated_at_utc = in.now_utc;
  v.expires_at_ns = SaturatingAdd(in.now_ns, kVerdictTtlNs);
  v.decision = Decision::Deny;
  v.gate_stage = 0;
  v.rule_id = "";

  auto deny = [&](int stage, const char* rc, std::string msg) -> Verdict {
    v.gate_stage = stage;
    v.decision = Decision::Deny;
    v.reason_code = rc ? rc : "";
    v.reason = std::move(msg);
    return v;
  };

  // ── 1. 입력 위생 (Input Hygiene) ─────────────────────────────────────────
  if (a.tool_name.empty() || a.tool_name.size() > 128) {
    return deny(1, reason::kInputMissingField, "Tool name is empty or exceeds 128 bytes limit");
  }
  if (!IsValidUtf8(a.tool_name)) {
    return deny(1, reason::kInputNotUtf8, "Tool name is not valid UTF-8");
  }

  auto serialized = ccj::Serialize(a.arguments);
  if (!serialized.ok()) {
    const Error& err = serialized.error();
    if (err.code == Errc::NotUtf8) {
      return deny(1, reason::kInputNotUtf8, "Tool arguments contain invalid UTF-8");
    }
    if (err.code == Errc::DepthExceeded) {
      return deny(1, reason::kInputDepthExceeded, "Tool arguments depth exceeds limit");
    }
    if (err.code == Errc::TooLarge) {
      return deny(1, reason::kInputTooLarge, "Tool arguments size exceeds limit");
    }
    return deny(1, reason::kInputMissingField, "Tool arguments serialization failed");
  }

  if (serialized.value().size() > limits_.max_action_bytes) {
    return deny(1, reason::kInputTooLarge, "Tool arguments byte size exceeds configured limit");
  }
  if (JsonDepth(a.arguments) > limits_.max_action_depth) {
    return deny(1, reason::kInputDepthExceeded, "Tool arguments depth exceeds configured limit");
  }

  // ── 2. 도구 등록 상태 (Registration Status / Tombstone vs Absent) ────────
  const LookupResult lk = registry_.Lookup(a.tool_name);
  if (lk.kind == LookupKind::Forbidden) {
    return deny(2, reason::kToolForbidden,
                (lk.desc && !lk.desc->forbidden_reason.empty())
                    ? lk.desc->forbidden_reason
                    : "Tool is forbidden (tombstone)");
  }
  if (lk.kind == LookupKind::Absent || lk.desc == nullptr) {
    return deny(2, reason::kToolNotRegistered,
                "Tool is not registered: " + a.tool_name);
  }
  const ToolDescriptor& td = *lk.desc;

  // ── 3. 도구 인자 스키마 검증 (Schema Validation) ─────────────────────────
  if (Error e = registry_.ValidateArguments(a.tool_name, a.arguments)) {
    v.gate_stage = 3;
    v.decision = Decision::Deny;
    v.reason_code = (e.code == Errc::PatternBudgetExhausted || e.reason_code == reason::kPatternTimeout)
                        ? reason::kPatternTimeout
                        : reason::kSchemaViolation;
    v.reason = e.message.empty() ? "Tool arguments failed schema validation" : e.message;
    return v;
  }

  // ── 4. FSM 상태 검증 (FSM State Validation) ─────────────────────────────
  if (in.fsm_state != State::Gate) {
    return deny(4, reason::kInvalidFsmState,
                "Current FSM state is not Gate");
  }

  // ── 5. 모드 상한 및 보안 정책 (Mode ceiling, Policy match, Lockdown) ─────
  if (const Error subject_error = ValidateSubject(in.subject)) {
    return deny(5, reason::kRoleDenied,
                subject_error.message.empty() ? "Subject is invalid" : subject_error.message);
  }
  if (!IsKnownEffect(td.effect) || !IsKnownMode(in.mode) ||
      static_cast<std::uint8_t>(td.effect) >
          static_cast<std::uint8_t>(ModeToMaxEffect(in.mode))) {
    return deny(5, reason::kModeDenied,
                "Tool effect exceeds execution mode upper bound");
  }

  Verdict pv = policy_.Evaluate(a.tool_name, td.effect, a.arguments, in.subject, in.mode);
  if (pv.decision == Decision::Deny) {
    v.gate_stage = 5;
    v.decision = Decision::Deny;
    v.reason_code = pv.reason_code.empty() ? reason::kPolicyDenied : pv.reason_code;
    v.reason = pv.reason.empty() ? "Policy denied tool execution" : pv.reason;
    v.rule_id = pv.rule_id;
    return v;
  }
  if (pv.decision != Decision::Ask && pv.decision != Decision::Allow) {
    return deny(5, reason::kPolicyDenied, "Policy returned an invalid decision");
  }
  v.rule_id = pv.rule_id;
  v.reason = pv.reason;

  // G0-04: indeterminate lockdown overrides Policy Allow
  if (in.indeterminate_locked) {
    return deny(5, reason::kIndeterminateLockdown,
                "Previous execution outcome is indeterminate; operator confirmation required");
  }

  // ── 6. 예산 및 리소스 제한 (Deadline & Resource Limits) ──────────────────
  if (const Error deadline_error = budget_.CheckDeadline(in.now_ns)) {
    return deny(6, reason::kBudgetDeadline,
                deadline_error.message.empty() ? "Turn deadline exceeded"
                                               : deadline_error.message);
  }
  if (budget_.tool_calls_count() >= budget_.budget().max_tool_calls) {
    return deny(6, reason::kBudgetToolCalls, "Turn tool call limit exceeded");
  }

  // ── 7. 작업자 승인 및 재진입 (Approval & Reentry Threshold) ─────────────
  const bool needs_approval =
      td.approval_required || pv.decision == Decision::Ask ||
      td.effect == Effect::Destructive || in.indeterminate_locked;

  if (!needs_approval) {
    v.gate_stage = 7;
    v.decision = Decision::Allow;
    v.reason_code = reason::kAllowed;
    if (v.reason.empty()) {
      v.reason = "Tool execution is allowed";
    }
    return v;
  }

  const ApprovalRecord* ar = (approvals_ != nullptr)
      ? approvals_->FindUsable(in.action_digest, in.permit_scope_digest, a.session_id, a.turn_id, in.now_ns)
      : nullptr;

  if (ar != nullptr) {
    if (ar->expires_at_ns <= in.now_ns) {
      return deny(7, reason::kApprovalExpired, "Approval has expired");
    }
    if (ar->consumed) {
      return deny(7, reason::kApprovalAlreadyConsumed, "Approval is already consumed");
    }
    if (ar->approval_id.empty() || ar->action_digest != in.action_digest ||
        ar->scope_digest != in.permit_scope_digest ||
        ar->session_id != a.session_id || ar->turn_id != a.turn_id) {
      return deny(7, reason::kApprovalScopeMismatch,
                  "Approval does not match the requested execution scope");
    }
    v.gate_stage = 7;
    v.decision = Decision::Allow;
    v.reason_code = reason::kAllowed;
    v.rule_id = "approval:" + ar->approval_id;
    v.reason = "Operator approval granted";
    return v;
  }

  // G0-31: gate_reentry_count upper bound. Deny on exceeding reentry limit.
  if (in.gate_reentry_count < 0 || limits_.max_reentry < 0 ||
      in.gate_reentry_count >= limits_.max_reentry) {
    return deny(7, reason::kApprovalReentryExceeded,
                "Approval procedure repeated; request terminated");
  }

  v.gate_stage = 7;
  v.decision = Decision::Ask;
  v.reason_code = reason::kApprovalRequired;
  if (v.reason.empty()) {
    v.reason = "Operator approval required for tool execution";
  }
  return v;
}

ExecutionPermit PermissionGate::IssuePermit(const ToolDescriptor& td,
                                            const Digest& action_digest,
                                            const Digest& scope_digest,
                                            std::int64_t now_ns) const {
  ExecutionPermit p;
  p.action_digest_ = action_digest;
  p.scope_digest_ = scope_digest;
  p.tool_name_ = td.name;
  p.idem_key_ = action_digest.hex();
  p.timeout_ms_ = td.timeout_ms;
  p.effect_ = IsKnownEffect(td.effect) ? td.effect : Effect::Destructive;
  constexpr std::int32_t kMaxTimeoutMs = 3'600'000;
  if (td.status != ToolStatus::Enabled || td.name.empty() ||
      !IsKnownEffect(td.effect) || td.timeout_ms <= 0 ||
      td.timeout_ms > kMaxTimeoutMs) {
    p.expires_ns_ = now_ns;
  } else {
    const std::int64_t timeout_ns =
        static_cast<std::int64_t>(td.timeout_ms) * 1'000'000LL;
    p.expires_ns_ = SaturatingAdd(now_ns, timeout_ns);
  }
  p.consumed_ = false;
  return p;
}

}  // namespace cogito
