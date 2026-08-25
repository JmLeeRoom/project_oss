// SPDX-License-Identifier: Apache-2.0
// Cogito++ — Action 요청 및 실행 정의
//
// 규범 근거 : Cogito++_구현명세서.md §4-2, §4-4, §4-12, 체크리스트 S2-01(:360-368)
#ifndef COGITO_ACTION_HPP
#define COGITO_ACTION_HPP

#include <cstdint>
#include <string>

#include "cogito/canonical_json.hpp"
#include "cogito/ids.hpp"

namespace cogito {

// ActionRequest — LLM 추론 또는 호출자로부터 전달된 단일 도구 실행 요청
struct ActionRequest {
  SessionId   session_id;      // 세션 ID (UUIDv4)
  TurnId      turn_id = 0;     // 단조 증가 턴 ID (1부터 시작)
  ActionId    action_id;       // 액션 ID (UUIDv4)
  std::string tool_name;       // 호출 대상 도구명
  ccj::Json   arguments;       // 도구 인자 JSON 객체 (ParseStrict 통과값)
  int         ordinal = 0;     // MVP에서는 단일 Action(0)만 허용
};

}  // namespace cogito

#endif  // COGITO_ACTION_HPP
