// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 주체 및 실행 모드 (Subject, ExecutionMode)
//
// 규범 근거 : Cogito++_구현명세서.md §4-7(:706-725), G0-29(docs/g0/G0-RESOLUTION-9.md ⑧)
#ifndef COGITO_IDENTITY_HPP
#define COGITO_IDENTITY_HPP

#include <cstdint>
#include <string>
#include <vector>

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

inline Effect ModeToMaxEffect(ExecutionMode mode) noexcept {
  switch (mode) {
    case ExecutionMode::Default:  return Effect::Destructive;
    case ExecutionMode::Edit:     return Effect::Write;
    case ExecutionMode::Plan:     return Effect::None;
    case ExecutionMode::ReadOnly: return Effect::None;
  }
  return Effect::None;
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

}  // namespace cogito

#endif  // COGITO_IDENTITY_HPP
