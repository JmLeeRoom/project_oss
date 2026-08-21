// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 식별자와 다이제스트 타입
//
// 규범 근거 : Cogito++_구현명세서.md §4-2, §6-1, §7-4
// G0 결정   : G0-26 (docs/g0/G0-RESOLUTION-9.md ⑦), ADR-0004 D5·D6
#ifndef COGITO_IDS_HPP
#define COGITO_IDS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "cogito/result.hpp"

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// Digest — SHA-256 원시 32바이트.
// 문자열이 아니다. LP 인코딩(ADR-0004 D5)에 넣을 때도 hex 가 아니라 원시 바이트다.
// ─────────────────────────────────────────────────────────────────────────────
struct Digest {
  static constexpr std::size_t kSize = 32;

  std::array<std::uint8_t, kSize> bytes{};   // 기본값 = 전부 0 (체인 시작값)

  bool operator==(const Digest& o) const noexcept { return bytes == o.bytes; }
  bool operator!=(const Digest& o) const noexcept { return !(*this == o); }
  // std::map/std::set 키로 쓸 수 있도록 — 사전식 비교
  bool operator<(const Digest& o) const noexcept { return bytes < o.bytes; }

  bool is_zero() const noexcept;

  // 소문자 hex 64자. idempotency_key 와 UI 표시에 쓴다.
  std::string hex() const;

  // 감사 체인의 첫 prev_hash. 문서화된 32바이트 zero 값이다(ADR-0004 D9).
  static Digest Zero() noexcept { return Digest{}; }

  // 소문자 hex 64자만 허용한다. 대문자·길이 불일치·비hex 문자는 거부한다.
  // (체크리스트 S1-02: "대문자 허용 여부를 명시적으로" — 불허가 규범이다)
  static Result<Digest> FromHex(const std::string& lowercase_hex);
};

// ─────────────────────────────────────────────────────────────────────────────
// 식별자
// ─────────────────────────────────────────────────────────────────────────────
using SessionId = std::string;   // UUIDv4 문자열 (소문자, 하이픈 포함 36자)
using ActionId  = std::string;   // UUIDv4 문자열
using TurnId    = std::uint64_t; // 세션 내 1부터 단조 증가. 0 은 "턴 없음"을 뜻한다

// action_id 가 비어도 되는 이벤트 종류(체크리스트 S1-02 요구).
//   turn_begin · turn_end · inference_requested · inference_result ·
//   audit_recovery · operator_ack
// 그 외 이벤트(verdict · tool_call_started · tool_result · approval_*)는
// 반드시 action_id 를 갖는다. AuditJournal 구현이 이를 강제한다.

// ─────────────────────────────────────────────────────────────────────────────
// 생성
// ─────────────────────────────────────────────────────────────────────────────

// 암호학적 난수원을 쓴다. version(4)·variant(10xx) 비트를 규격대로 설정한다.
// 난수원 실패를 조용히 약한 난수로 대체하지 않는다 — 실패는 오류로 전달한다.
Result<std::string> NewUuidV4();

// 프로세스 시작 시 정확히 한 번 생성한다. 모든 감사 이벤트에 실린다(§7-4).
// monotonic_ns 비교는 같은 process_epoch_id 안에서만 유효하다.
const std::string& ProcessEpochId();

// 프로세스 epoch 를 초기화한다. main() 진입 직후 한 번만 호출한다.
// 두 번째 호출은 Errc::Internal 이다.
[[nodiscard]] Error InitProcessEpoch();

}  // namespace cogito

#endif  // COGITO_IDS_HPP
