// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 대화 저장소 및 컨텍스트 축약 구현

#include "cogito/conversation.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cogito/canonical_json.hpp"
#include "cogito/context_compactor.hpp"
#include "cogito/result.hpp"

namespace cogito {
namespace {

std::string DumpJson(const ccj::Json& j) {
  auto res = ccj::Serialize(j);
  if (res.ok()) {
    return res.value();
  }
  return j.dump();
}

std::size_t EstimateMessageBytes(const Message& m) {
  std::size_t bytes = m.content.size();
  bytes += m.provenance.size();
  bytes += m.tool_call_id.size();
  for (const auto& act : m.actions) {
    bytes += act.tool_name.size();
    bytes += act.action_id.size();
    bytes += act.session_id.size();
    bytes += DumpJson(act.arguments).size();
  }
  return bytes;
}

std::size_t TotalMessagesBytes(const std::vector<Message>& msgs) {
  std::size_t total = 0;
  for (const auto& m : msgs) {
    total += EstimateMessageBytes(m);
  }
  return total;
}

class DropOldestObservationCompactor : public ContextCompactor {
 public:
  const std::string& version() const noexcept override {
    static const std::string kVersion = "drop-oldest-observation-v1.0";
    return kVersion;
  }

  Result<CompactionResult> CompactIfNeeded(
      std::vector<Message>* messages,
      std::size_t context_soft_limit_bytes) override {
    if (messages == nullptr) {
      return Error{Errc::InvalidArgument, reason::kInputMissingField,
                   "Messages vector is null"};
    }

    std::size_t bytes_before = TotalMessagesBytes(*messages);
    std::size_t messages_before = messages->size();

    if (bytes_before <= context_soft_limit_bytes) {
      CompactionResult res;
      res.compacted = false;
      res.messages_before = messages_before;
      res.messages_after = messages_before;
      res.bytes_before = bytes_before;
      res.bytes_after = bytes_before;
      res.removed_first = 0;
      res.removed_last = 0;
      res.compactor_version = version();
      return res;
    }

    // Identify latest Role::Tool message index (protected from dropping)
    std::size_t latest_tool_idx = static_cast<std::size_t>(-1);
    for (std::size_t i = messages->size(); i > 0; --i) {
      if ((*messages)[i - 1].role == Role::Tool) {
        latest_tool_idx = i - 1;
        break;
      }
    }

    std::vector<std::size_t> to_remove;
    std::size_t current_bytes = bytes_before;

    for (std::size_t i = 0; i < messages->size(); ++i) {
      if (current_bytes <= context_soft_limit_bytes) {
        break;
      }

      const auto& msg = (*messages)[i];
      // Protected messages:
      // 1. Role::System
      // 2. Messages with actions (pending action requests)
      // 3. Most recent Tool observation (latest_tool_idx)
      if (msg.role == Role::Tool && i != latest_tool_idx) {
        to_remove.push_back(i);
        current_bytes -= EstimateMessageBytes(msg);
      }
    }

    if (to_remove.empty()) {
      CompactionResult res;
      res.compacted = false;
      res.messages_before = messages_before;
      res.messages_after = messages_before;
      res.bytes_before = bytes_before;
      res.bytes_after = bytes_before;
      res.removed_first = 0;
      res.removed_last = 0;
      res.compactor_version = version();
      return res;
    }

    std::size_t removed_first = to_remove.front();
    std::size_t removed_last = to_remove.back() + 1;

    std::vector<Message> remaining;
    remaining.reserve(messages->size() - to_remove.size());

    std::size_t remove_pos = 0;
    for (std::size_t i = 0; i < messages->size(); ++i) {
      if (remove_pos < to_remove.size() && to_remove[remove_pos] == i) {
        ++remove_pos;
      } else {
        remaining.push_back(std::move((*messages)[i]));
      }
    }

    *messages = std::move(remaining);
    std::size_t bytes_after = TotalMessagesBytes(*messages);

    CompactionResult res;
    res.compacted = true;
    res.messages_before = messages_before;
    res.messages_after = messages->size();
    res.bytes_before = bytes_before;
    res.bytes_after = bytes_after;
    res.removed_first = removed_first;
    res.removed_last = removed_last;
    res.compactor_version = version();
    return res;
  }
};

class NoopCompactor : public ContextCompactor {
 public:
  const std::string& version() const noexcept override {
    static const std::string kVersion = "none-v1.0";
    return kVersion;
  }

  Result<CompactionResult> CompactIfNeeded(
      std::vector<Message>* messages,
      std::size_t context_soft_limit_bytes) override {
    (void)context_soft_limit_bytes;
    CompactionResult res;
    res.compacted = false;
    if (messages != nullptr) {
      res.messages_before = messages->size();
      res.messages_after = messages->size();
      res.bytes_before = TotalMessagesBytes(*messages);
      res.bytes_after = res.bytes_before;
    }
    res.removed_first = 0;
    res.removed_last = 0;
    res.compactor_version = version();
    return res;
  }
};

}  // namespace

std::unique_ptr<ContextCompactor> MakeDropOldestObservationCompactor() {
  return std::make_unique<DropOldestObservationCompactor>();
}

std::unique_ptr<ContextCompactor> MakeNoopCompactor() {
  return std::make_unique<NoopCompactor>();
}

}  // namespace cogito
