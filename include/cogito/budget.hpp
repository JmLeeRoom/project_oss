// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 턴 예산 및 자원 추적 (BudgetTracker, TurnBudget)
//
// 규범 근거 : Cogito++_구현명세서.md §4-12, 체크리스트 S2-01
#ifndef COGITO_BUDGET_HPP
#define COGITO_BUDGET_HPP

#include <cstddef>
#include <cstdint>

#include "cogito/result.hpp"

namespace cogito {

struct TurnBudget {
  std::size_t   max_tool_calls = 10;                  // 턴당 최대 도구 호출 횟수
  std::size_t   max_total_output_bytes = 1024 * 1024; // 턴당 최대 총 출력 바이트 (1MB)
  std::int64_t  timeout_ns = 30'000'000'000LL;        // 턴 총 제한시간 (30s)
};

class BudgetTracker {
 public:
  explicit BudgetTracker(const TurnBudget& budget = TurnBudget{})
      : budget_(budget) {}

  Error RecordToolCall(std::size_t output_bytes = 0) {
    if (tool_calls_count_ >= budget_.max_tool_calls) {
      return Error{Errc::BudgetExhausted, reason::kBudgetToolCalls, "Turn tool call limit exceeded"};
    }
    // 오버플로우 방지 및 상한 검사
    if (output_bytes > budget_.max_total_output_bytes ||
        total_output_bytes_ > budget_.max_total_output_bytes - output_bytes) {
      return Error{Errc::BudgetExhausted, reason::kBudgetTokens, "Turn total output byte limit exceeded"};
    }
    tool_calls_count_++;
    total_output_bytes_ += output_bytes;
    return Error::Ok();
  }

  std::size_t tool_calls_count() const noexcept { return tool_calls_count_; }
  std::size_t total_output_bytes() const noexcept { return total_output_bytes_; }
  const TurnBudget& budget() const noexcept { return budget_; }

 private:
  TurnBudget  budget_;
  std::size_t tool_calls_count_ = 0;
  std::size_t total_output_bytes_ = 0;
};

}  // namespace cogito

#endif  // COGITO_BUDGET_HPP
