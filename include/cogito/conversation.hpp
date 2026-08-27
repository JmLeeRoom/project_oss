// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 대화 저장소 및 메시지 정의 (ConversationStore & Message)
//
// 규범 근거 : Cogito++_구현명세서.md §4-12(:1040-1060), §3 불변식 10, 체크리스트 S6-01
// G0 결정   : G0-25 (Accepted)
//
// [불변 대화 원칙]
// 1. 외부 데이터 표시: Tool/RAG/MCP 유래 메시지는 untrusted=true 플래그를 보존한다.
// 2. 보호 메시지 보존: Context 축약 시 시스템 정책, 미결 Action, 최신 Tool 결과는 제거하지 않는다.
// 3. 결정론적 재생: 같은 메시지 시퀀스는 동일한 직렬화 및 토큰 추정을 산출한다.
#ifndef COGITO_CONVERSATION_HPP
#define COGITO_CONVERSATION_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cogito/action.hpp"
#include "cogito/canonical_json.hpp"
#include "cogito/context_compactor.hpp"
#include "cogito/ids.hpp"
#include "cogito/result.hpp"

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// Role — 대화 메시지 발신 역할
// ─────────────────────────────────────────────────────────────────────────────
enum class Role : std::uint8_t {
  System    = 1,
  User      = 2,
  Assistant = 3,
  Tool      = 4,
};

inline constexpr std::string_view ToString(Role role) noexcept {
  switch (role) {
    case Role::System:    return "system";
    case Role::User:      return "user";
    case Role::Assistant: return "assistant";
    case Role::Tool:      return "tool";
  }
  return "unknown";
}

inline Result<Role> ParseRole(std::string_view sv) noexcept {
  if (sv == "system")    return Role::System;
  if (sv == "user")      return Role::User;
  if (sv == "assistant") return Role::Assistant;
  if (sv == "tool")      return Role::Tool;
  return Error{Errc::InvalidArgument, "", "Unknown role string"};
}

// ─────────────────────────────────────────────────────────────────────────────
// Message — 단일 대화 턴 레코드
// ─────────────────────────────────────────────────────────────────────────────
struct Message {
  Role                       role = Role::User;
  std::string                content;
  ActionId                   tool_call_id;          // Role::Tool 일 때 대응 ActionId
  std::vector<ActionRequest> actions;               // Role::Assistant 일 때 요청된 Actions
  bool                       untrusted = false;     // 외부 도구/RAG/MCP 결과 표시 (불변식 10)
  std::string                provenance;            // 출처 식별자 (예: "tool:opcua.read.v1")
};

// ─────────────────────────────────────────────────────────────────────────────
// ConversationStore — 세션 대화 히스토리 보관 및 축약 관리자
// ─────────────────────────────────────────────────────────────────────────────
class ConversationStore {
 public:
  ConversationStore() = default;

  // 메시지 추가 (deep copy)
  void AddMessage(Message message) {
    messages_.push_back(std::move(message));
  }

  // 시스템 정책 메시지 주입/교체
  void SetSystemPrompt(std::string prompt) {
    if (!messages_.empty() && messages_.front().role == Role::System) {
      messages_.front().content = std::move(prompt);
      messages_.front().untrusted = false;
      messages_.front().provenance = "system:policy";
    } else {
      Message sys;
      sys.role = Role::System;
      sys.content = std::move(prompt);
      sys.untrusted = false;
      sys.provenance = "system:policy";
      messages_.insert(messages_.begin(), std::move(sys));
    }
  }

  // 전체 메시지 조회
  const std::vector<Message>& messages() const noexcept { return messages_; }
  std::vector<Message>& mutable_messages() noexcept { return messages_; }

  std::size_t size() const noexcept { return messages_.size(); }
  bool empty() const noexcept { return messages_.empty(); }

  void Clear() noexcept { messages_.clear(); }

  // 컨텍스트 용량 초과 시 결정론적 축약 수행
  Result<CompactionResult> Compact(ContextCompactor& compactor,
                                   std::size_t context_soft_limit_bytes) {
    return compactor.CompactIfNeeded(&messages_, context_soft_limit_bytes);
  }

 private:
  std::vector<Message> messages_;
};

}  // namespace cogito

#endif  // COGITO_CONVERSATION_HPP