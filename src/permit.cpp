// SPDX-License-Identifier: Apache-2.0

#include "cogito/permit.hpp"

#include <utility>

namespace cogito {

const std::int64_t kVerdictTtlNs = 60'000'000'000LL;

ExecutionPermit::ExecutionPermit(ExecutionPermit&& other) noexcept
    : action_digest_(std::move(other.action_digest_)),
      scope_digest_(std::move(other.scope_digest_)),
      tool_name_(std::move(other.tool_name_)),
      idem_key_(std::move(other.idem_key_)),
      expires_ns_(other.expires_ns_),
      timeout_ms_(other.timeout_ms_),
      effect_(other.effect_),
      consumed_(other.consumed_) {
  other.action_digest_ = Digest{};
  other.scope_digest_ = Digest{};
  other.tool_name_.clear();
  other.idem_key_.clear();
  other.expires_ns_ = 0;
  other.timeout_ms_ = 0;
  other.effect_ = Effect::Destructive;
  other.consumed_ = true;
}

ExecutionPermit& ExecutionPermit::operator=(ExecutionPermit&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  action_digest_ = std::move(other.action_digest_);
  scope_digest_ = std::move(other.scope_digest_);
  tool_name_ = std::move(other.tool_name_);
  idem_key_ = std::move(other.idem_key_);
  expires_ns_ = other.expires_ns_;
  timeout_ms_ = other.timeout_ms_;
  effect_ = other.effect_;
  consumed_ = other.consumed_;

  other.action_digest_ = Digest{};
  other.scope_digest_ = Digest{};
  other.tool_name_.clear();
  other.idem_key_.clear();
  other.expires_ns_ = 0;
  other.timeout_ms_ = 0;
  other.effect_ = Effect::Destructive;
  other.consumed_ = true;
  return *this;
}

ExecutionPermit::~ExecutionPermit() = default;

Error ExecutionPermit::CheckUsable(const std::string& expected_tool_name,
                                   const Digest& expected_scope_digest,
                                   std::int64_t now_ns) const {
  if (tool_name_.empty()) {
    return Error{Errc::Internal, {}, "Execution permit is invalid"};
  }
  if (IsExpired(now_ns)) {
    return Error{Errc::ApprovalInvalid, reason::kApprovalExpired,
                 "Execution permit has expired"};
  }
  if (consumed_) {
    return Error{Errc::ApprovalInvalid, reason::kApprovalAlreadyConsumed,
                 "Execution permit has already been consumed"};
  }
  if (tool_name_ != expected_tool_name) {
    return Error{Errc::ApprovalInvalid, reason::kApprovalScopeMismatch,
                 "Execution permit tool does not match"};
  }
  if (scope_digest_ != expected_scope_digest) {
    return Error{Errc::ApprovalInvalid, reason::kApprovalScopeMismatch,
                 "Execution permit scope does not match"};
  }
  return Error::Ok();
}

}  // namespace cogito
