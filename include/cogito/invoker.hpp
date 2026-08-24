// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 도구 실행자
//
// 규범 근거 : Cogito++_구현명세서.md §4-12, §6-2-a, §3(불변식 1·9)
// G0 결정   : G0-25 (docs/g0/G0-RESOLUTION-9.md ⑥), G0-27(output schema 검증 시점)
//             G0-09 규칙 3(예외), G0-24 R3(취소 분류)
//
// 불변식 1 — "유효한 미소비 Permit 없이는 Tool handler 에 도달하지 않는다" 를
// 타입 수준에서 강제하는 지점이다. Gate 밖의 공개 실행 진입점을 만들지 않는다.
//
// ⚠ 선반영 고지 — 이 파일은 G0-RESOLUTION-9 의 **Proposed** 결정을 선반영한
//    초안이며 승인 전에는 규범이 아니다. 승인 전까지 이 헤더를 구현 기준으로 인계하지 않는다.
//    승인 시 이 고지를 제거한다. (승인 상태: docs/g0/G0-LEDGER.md)
//    ※ 이 헤더가 참조하는 G0-27(output schema 검증 시점)은 G0-RESOLUTION-9 의 9건에
//      포함되지 않은 **미해소** 항목이다. 해당 계약은 승인 대기가 아니라 미결이다.
#ifndef COGITO_INVOKER_HPP
#define COGITO_INVOKER_HPP

#include <cstdint>
#include <string>

#include "cogito/canonical_json.hpp"
#include "cogito/ids.hpp"
#include "cogito/permit.hpp"
#include "cogito/result.hpp"
#include "cogito/tool.hpp"

namespace cogito {

class Clock;
class ToolRegistry;
class CancelToken;
struct Subject;

// ─────────────────────────────────────────────────────────────────────────────
struct ToolCallContext {
  // lowercase_hex(action_digest). ADR-0004 D7 — operation_digest 가 아니다.
  // 불변식 9 가 자동 재시도를 금지하므로 다음 턴의 재제안은 '새 작업'이고 새 키를 받는다.
  std::string idempotency_key;

  // monotonic 기준 절대 시각. wall clock 이 아니다.
  std::int64_t deadline_ns = 0;

  // R3 — Execute 는 Cancel 을 FSM 이벤트로 받지 않는다.
  // 취소는 이 토큰으로만 전달되고 결과가 ToolResult 로 분류된다.
  const CancelToken* cancel = nullptr;

  // 감사 귀속용. handler 에 넘기지 않는다(도구가 주체를 신뢰하게 만들지 않는다).
  const Subject* subject = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
class ToolInvoker {
 public:
  ToolInvoker(const ToolRegistry& registry, const Clock& clock)
      : reg_(registry), clock_(clock) {}

  ToolInvoker(const ToolInvoker&) = delete;
  ToolInvoker& operator=(const ToolInvoker&) = delete;

  // 도구를 실행한다.
  //
  // permit 은 rvalue 로만 받는다 — 호출부에서 Permit 이 소비·소멸되므로
  // 같은 허가로 두 번 실행할 수 없다.
  //
  // [실행 계약]
  //   1. Permit 검증 — valid, 미소비, 미만료, tool_name 일치, scope 일치.
  //      하나라도 어긋나면 handler 를 호출하지 않고 ToolResult{Error} 를 낸다.
  //   2. Permit 소비 — handler 호출 '직전'에 정확히 한 번.
  //   3. handler 호출은 최대 1회. 재시도하지 않는다(불변식 9).
  //   4. 결과 검증 — 크기 상한 -> JSON 파싱 -> output schema 순(G0-27).
  //      위반 시 ToolResultStatus::Error 이며 대화에는 원문 대신
  //      구조화된 invalid_tool_result 만 주입한다(요구사항 §3).
  //   5. 예외 — 이 함수는 예외를 밖으로 내보내지 않는다. handler 가 던진 예외를
  //      자체적으로 잡아 ToolResult{Error} 로 변환한다(G0-09 규칙 3).
  //      최외곽 ABI 가드에만 의존하면 §6-2-a 의 tool_result 커밋을 건너뛰고
  //      FSM 이 Execute 에 남는다.
  //   6. 취소 분류 (ADR-0001 R3)
  //        effect == None  -> ToolResultStatus::Cancelled
  //        effect != None  -> ToolResultStatus::Indeterminate
  //      쓰기 중 취소를 '깨끗한 취소'로 기록하지 않는다.
  //   7. timeout — deadline 초과 시 ToolResultStatus::Timeout.
  //      effect != None 이면 Timeout 이 아니라 Indeterminate 다(설비 상태를 알 수 없다).
  //
  // 이 함수는 noexcept 로 선언하지 않는다(내부에서 할당이 일어난다).
  // 그러나 예외를 밖으로 내보내지 않는 것이 계약이다.
  //
  // @thread: agent-loop-only
  ToolResult Invoke(ExecutionPermit&& permit,
                    const ccj::Json& arguments,
                    const ToolCallContext& ctx);

 private:
  const ToolRegistry& reg_;
  const Clock&        clock_;
};

// 도구 결과가 계약을 위반했을 때 대화에 주입하는 구조체.
// 원문을 재주입하지 않는다(불변식 10 — 크기·프롬프트 주입 표면).
ccj::Json MakeInvalidToolResult(const std::string& tool_name,
                                const std::string& reason_code,
                                const std::string& detail_masked);

}  // namespace cogito

#endif  // COGITO_INVOKER_HPP
