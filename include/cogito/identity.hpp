// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 주체 및 실행 모드 (Subject, ExecutionMode)
//
// 규범 근거 : Cogito++_구현명세서.md §4-7(:706-725), G0-29(docs/g0/G0-RESOLUTION-9.md ⑧)
#ifndef COGITO_IDENTITY_HPP
#define COGITO_IDENTITY_HPP

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cogito/result.hpp"
#include "cogito/tool.hpp"

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// ExecutionMode — 실행 모드
//
// [모드-효과 사상 및 G0-29 규칙]
// - ExecutionMode enum 은 권한의 선형 정렬이 아니므로 숫자로 직접 비교(min/max)하지 않는다.
// - 모드를 "허용 effect 상한"으로 사상한 뒤 최솟값을 취한다:
//     Default  -> Effect::Destructive (최상위)
//     Edit     -> Effect::Write
//     Plan     -> Effect::None
//     ReadOnly -> Effect::None
// ─────────────────────────────────────────────────────────────────────────────
enum class ExecutionMode : std::uint8_t {
  Default = 0,
  Plan = 1,
  Edit = 2,
  ReadOnly = 3
};

inline const char* ToString(ExecutionMode mode) noexcept {
  switch (mode) {
    case ExecutionMode::Default:  return "default";
    case ExecutionMode::Plan:     return "plan";
    case ExecutionMode::Edit:     return "edit";
    case ExecutionMode::ReadOnly: return "readonly";
  }
  return "readonly";
}

inline Result<ExecutionMode> ParseExecutionMode(std::string_view sv) noexcept {
  if (sv == "default" || sv == "Default" || sv == "*") {
    return ExecutionMode::Default;
  }
  if (sv == "plan" || sv == "Plan") {
    return ExecutionMode::Plan;
  }
  if (sv == "edit" || sv == "Edit") {
    return ExecutionMode::Edit;
  }
  if (sv == "readonly" || sv == "ReadOnly") {
    return ExecutionMode::ReadOnly;
  }
  return Error{Errc::InvalidArgument, "", "Unknown execution mode"};
}

inline Effect ModeToMaxEffect(ExecutionMode mode) noexcept {
  switch (mode) {
    case ExecutionMode::Default:  return Effect::Destructive;
    case ExecutionMode::Edit:     return Effect::Write;
    case ExecutionMode::Plan:     return Effect::None;
    case ExecutionMode::ReadOnly: return Effect::None;
  }
  return Effect::None;
}

// G0-29 결합 규칙: effective_cap = min_effect(cap(config.mode), cap(requested.mode))
inline Effect ResolveEffectiveMaxEffect(ExecutionMode requested, ExecutionMode configured) noexcept {
  const auto req_eff = static_cast<std::uint8_t>(ModeToMaxEffect(requested));
  const auto cfg_eff = static_cast<std::uint8_t>(ModeToMaxEffect(configured));
  return static_cast<Effect>(std::min(req_eff, cfg_eff));
}

// 요청 모드가 설정 모드 상한을 초과할 경우 설정 상한에 맞는 모드로 제한
inline ExecutionMode ResolveExecutionMode(ExecutionMode requested, ExecutionMode configured_max) noexcept {
  const Effect effective_eff = ResolveEffectiveMaxEffect(requested, configured_max);
  if (ModeToMaxEffect(requested) == effective_eff) {
    return requested;
  }
  switch (effective_eff) {
    case Effect::Destructive: return ExecutionMode::Default;
    case Effect::Write:       return ExecutionMode::Edit;
    case Effect::None:        return (requested == ExecutionMode::Plan) ? ExecutionMode::Plan : ExecutionMode::ReadOnly;
  }
  return ExecutionMode::ReadOnly;
}

// ─────────────────────────────────────────────────────────────────────────────
// Subject — 요청 주체
// ─────────────────────────────────────────────────────────────────────────────
struct Subject {
  std::string              subject_id;    // 인증원이 부여한 안정 ID (예: "user-operator-01")
  std::vector<std::string> roles;         // 주체 역할 목록 (예: {"operator", "qa_engineer"})
  std::string              auth_method;   // "os_user" | "badge" | "oidc" | "mtls"
  std::string              line_id;       // 설비/라인 스코프 (없으면 빈 문자열)
};

inline Error ValidateSubject(const Subject& subject) {
  if (subject.subject_id.empty()) {
    return Error{Errc::InvalidArgument, reason::kInputMissingField,
                 "subject_id must not be empty"};
  }
  if (subject.subject_id.size() > 128) {
    return Error{Errc::TooLarge, reason::kInputTooLarge,
                 "subject_id cannot exceed 128 bytes"};
  }
  if (subject.auth_method.empty()) {
    return Error{Errc::InvalidArgument, reason::kInputMissingField,
                 "auth_method cannot be empty"};
  }
  if (subject.auth_method != "os_user" &&
      subject.auth_method != "badge" &&
      subject.auth_method != "oidc" &&
      subject.auth_method != "mtls") {
    return Error{Errc::InvalidArgument, "",
                 "auth_method must be one of 'os_user', 'badge', 'oidc', 'mtls'"};
  }
  if (subject.line_id.size() > 128) {
    return Error{Errc::TooLarge, reason::kInputTooLarge,
                 "line_id cannot exceed 128 bytes"};
  }
  for (const auto& r : subject.roles) {
    if (r.empty()) {
      return Error{Errc::InvalidArgument, reason::kInputMissingField,
                   "role names must not be empty"};
    }
    if (r.size() > 64) {
      return Error{Errc::TooLarge, reason::kInputTooLarge,
                   "role name cannot exceed 64 bytes"};
    }
  }
  return Error::Ok();
}

inline void NormalizeSubject(Subject& subject) {
  std::sort(subject.roles.begin(), subject.roles.end());
  subject.roles.erase(std::unique(subject.roles.begin(), subject.roles.end()),
                      subject.roles.end());
}

}  // namespace cogito

#endif  // COGITO_IDENTITY_HPP
