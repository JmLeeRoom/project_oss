// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 대화 컨텍스트 축약
//
// 규범 근거 : Cogito++_구현명세서.md §4-12, §10-1(골든 리플레이 키), 요구사항 §4-2
// G0 결정   : G0-25 (docs/g0/G0-RESOLUTION-9.md ⑥)
//
// ★ 이 컴포넌트는 결정론 계약의 일부다.
//   축약이 일어나면 다음 추론의 프롬프트가 달라지고, 그러면 실행 경로가 달라진다.
//   그래서 compactor 버전이 §10-1 골든 리플레이 키의 구성요소다.
//   버전이 바뀌면 골든이 '깨져야' 한다 — 조용히 통과하면 재현성이 거짓이 된다.
//
// ⚠ 선반영 고지 — 이 파일은 G0-RESOLUTION-9 의 **Proposed** 결정을 선반영한
//    초안이며 승인 전에는 규범이 아니다. 승인 전까지 이 헤더를 구현 기준으로 인계하지 않는다.
//    승인 시 이 고지를 제거한다. (docs/STATUS-AUDIT-2026-08-24.md §2-⑥)
#ifndef COGITO_CONTEXT_COMPACTOR_HPP
#define COGITO_CONTEXT_COMPACTOR_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cogito/result.hpp"

namespace cogito {

struct Message;

// ─────────────────────────────────────────────────────────────────────────────
struct CompactionResult {
  bool        compacted = false;   // false 면 아무것도 바꾸지 않았다

  std::size_t messages_before = 0;
  std::size_t messages_after  = 0;
  std::size_t bytes_before    = 0;
  std::size_t bytes_after     = 0;

  // 축약된 원본 범위 [first, last) — 감사에 남긴다(요구사항 §4-2:
  // "Context 축약 결과도 원본 범위와 compactor 버전을 감사한다").
  std::size_t removed_first = 0;
  std::size_t removed_last  = 0;

  std::string compactor_version;   // 감사·골든 키에 실린다
};

// ─────────────────────────────────────────────────────────────────────────────
class ContextCompactor {
 public:
  virtual ~ContextCompactor() = default;

  // 골든 리플레이 키 구성요소(§10-1). 알고리즘이 바뀌면 반드시 올린다.
  // 형식: "<algorithm>-v<major>.<minor>"  예) "drop-oldest-observation-v1.0"
  virtual const std::string& version() const noexcept = 0;

  // context_soft_limit 을 넘으면 축약한다. 넘지 않으면 compacted=false 로 반환한다.
  //
  // ★ 절대 제거하지 않는 것 (요구사항 §4-2)
  //     1. system 정책 메시지
  //     2. 현재 pending 상태인 Action 과 그 승인 요청
  //     3. 가장 최근의 Tool 결과
  //   이 셋 중 하나라도 제거하면 Gate 재평가와 승인 화면이 근거를 잃는다.
  //
  // ★ 결정론 요구
  //   같은 입력 메시지 열 + 같은 한도 -> 같은 출력이어야 한다.
  //   시각·난수·해시 순회 순서에 의존하는 구현을 금지한다.
  //
  // ★ 외부 데이터 취급 (불변식 10)
  //   untrusted 메시지를 요약해 합칠 때, 요약 결과도 untrusted 로 유지한다.
  //   요약 과정에서 provenance 를 잃지 않는다.
  //
  // @thread: agent-loop-only
  [[nodiscard]] virtual Result<CompactionResult> CompactIfNeeded(
      std::vector<Message>* messages,
      std::size_t context_soft_limit_bytes) = 0;
};

// 기본 구현 — 가장 오래된 '관측(Tool 결과)' 메시지부터 제거한다.
// 보존 필수 항목은 건너뛴다. 요약하지 않으므로 모델 호출이 없고 결정론적이다.
// version() == "drop-oldest-observation-v1.0"
std::unique_ptr<ContextCompactor> MakeDropOldestObservationCompactor();

// 축약을 하지 않는 구현. 테스트와 짧은 턴 프로파일용.
// version() == "none-v1.0"
std::unique_ptr<ContextCompactor> MakeNoopCompactor();

}  // namespace cogito

#endif  // COGITO_CONTEXT_COMPACTOR_HPP
