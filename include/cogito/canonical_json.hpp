// SPDX-License-Identifier: Apache-2.0
// Cogito++ — CCJ v1 정규 직렬화 (RFC 8785 서브셋 / C99 %g 골든 규칙)
//
// 규범 근거 : Cogito++_구현명세서.md §2(레이아웃 65), §3-1(156-169), §3-1-a(172-201),
//             §4-3(445-473), §6-1(1401-1440)
// G0 결정   : G0-23 (docs/g0/G0-RESOLUTION-9.md ④ — Accepted), ADR-0004 D1·D2·D3·D4 (Accepted)
//
// digest·감사 payload·골든 비교의 **유일한** 직렬화 경로다(§3-1:153).
// `nlohmann::json::dump()` 를 digest 입력에 쓰는 것을 금지한다 — 키 순서와 숫자 표기가
// 구현·플랫폼마다 달라지면 같은 Action 이 다른 `action_digest` 를 내고,
// 승인이 action 에 결합된다는 불변식 5 와 감사 해시체인이 동시에 무너진다.
// 이 헤더가 확정하는 것은 의미가 아니라 **바이트**다. 의미가 같아도 바이트가 다르면 틀린 것이다.
#ifndef COGITO_CANONICAL_JSON_HPP
#define COGITO_CANONICAL_JSON_HPP

#include <cstddef>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "cogito/result.hpp"

namespace cogito::ccj {

// ─────────────────────────────────────────────────────────────────────────────
// Json — JSON 값 타입
//
// §4-3:455 가 `using Json = nlohmann::json;` 을 규정한다.
// 판별·접근은 nlohmann 의 것을 그대로 쓴다.
//   null/bool        : is_null(), is_boolean(), get<bool>()
//   정수/실수        : is_number_integer(), is_number_unsigned(), is_number_float(),
//                      get<std::int64_t>(), get<std::uint64_t>(), get<double>()
//   문자열/배열/객체 : is_string(), is_array(), is_object()
//
// ★ 예외 규율 — 코어 예외는 C ABI 경계를 넘지 않는다(G0-09 R3).
//   nlohmann 이 `JSON_NOEXCEPTION` 으로 빌드되더라도 이 헤더의 함수들은
//   예외에 의존하지 않고 항상 Result/Error 로 실패를 반환한다.
// ─────────────────────────────────────────────────────────────────────────────
using Json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// ParseStrict — 거부하는 파서 (§4-3:460-467)
//
// nlohmann 기본 파서의 조용한 덮어쓰기를 방지하고, 중복 키·비UTF-8·깊이·크기를 거부한다.
//
// [거부 사유 — Errc 매핑]
//   중복 키                 -> Errc::DuplicateKey    (reason::kInputDuplicateKey)
//   비 UTF-8 바이트         -> Errc::NotUtf8         (reason::kInputNotUtf8)
//   깊이 초과               -> Errc::DepthExceeded   (reason::kInputDepthExceeded)
//   크기 초과               -> Errc::TooLarge        (reason::kInputTooLarge)
//   객체별 키 개수 초과     -> Errc::TooLarge        (reason::kInputTooLarge)
// ─────────────────────────────────────────────────────────────────────────────
struct ParseLimits {
  // §4-3:463-465 가 정한 입력 상한
  std::size_t max_bytes = 256 * 1024;  // 전체 입력 바이트 상한 (256 KB)
  int         max_depth = 32;          // 중첩 깊이 상한 (32 단계)
  std::size_t max_keys  = 512;         // [명문화] 단일 JSON 객체 하나당(per-object) 키 수 상한
};

// UTF-8 원문 바이트를 받아 엄격하게 검증하며 파싱한다.
// @thread: any — 전역 상태를 읽거나 쓰지 않으며 로케일 독립적이다.
Result<Json> ParseStrict(std::string_view text, const ParseLimits& lim = ParseLimits{});

// ─────────────────────────────────────────────────────────────────────────────
// Serialize — CCJ v1 정규 직렬화 (§3-1:156-169, G0-23 / ADR-0004 확정)
//
// [CCJ-1] 공백 없음. 구분자는 `,` 와 `:` 만 사용.
//
// [CCJ-2] 객체 키는 **UTF-16 code unit 오름차순**으로 정렬한다 (RFC 8785 §3.2.3).
//         - 단순 UTF-8 바이트순이 아니며, UTF-16 code unit 순으로 비교한다.
//         - 중복 키는 ParseStrict 에서 이미 거부되므로 동률 키는 존재하지 않는다.
//
// [CCJ-3] 배열 순서 엄격 보존. 재정렬 금지.
//
// [CCJ-4] 문자열 인코딩 및 유니코드 보존 (RFC 8785 §3.2.2.2):
//         - `"` `\` 와 U+0000~U+001F 제어문자만 이스케이프한다.
//         - `\b`, `\f`, `\n`, `\r`, `\t` 는 짧은 2글자 형식, 나머지 제어문자는 소문자 `\u00xx`.
//         - **유효한 UTF-8 원본 바이트열을 그대로 보존한다.**
//         - **NFC 정규화를 강제하거나 non-NFC 를 거부하지 않는다** (입력 훼손 방지).
//         - 유효하지 않은 UTF-8 바이트 시퀀스는 직렬화 시 `Errc::NotUtf8` 로 거부한다.
//
// [CCJ-5] 숫자 직렬화 (G0-23 / ADR-0004 D1 확정 — 골든표 권위):
//         - 64비트 정수 범위 내의 정수형 숫자는 십진 정수 문자열로 출력한다.
//           예: 2^53+2 -> "9007199254740994" (정확한 십진수 보존)
//         - -0.0 은 "0" 으로 정규화한다.
//         - 부동소수점 실수는 C 로케일에서 %.15g -> %.16g -> %.17g 순으로 왕복 비트 동일성
//           (shortest round-trip)을 검증하여 출력한다.
//         - 지수 표기는 **C99 %g 규칙**을 따른다: 소문자 'e', 부호 필수, **지수 최소 2자리**
//           (예: 1e-7 -> "1e-07", -1e-7 -> "-1e-07"). 선행 0을 제거하지 않는다.
//         - NaN 및 Infinity 는 `Errc::InvalidArgument` 로 거부한다 (fail-closed).
//
// [CCJ-6] 불리언 및 널: `true`, `false`, `null` 소문자 출력.
//
// [CCJ-7] 안전 상한:
//         - 직렬화 도중 중첩 깊이가 64를 초과하면 `Errc::DepthExceeded` 로 실패한다.
//         - 직렬화 결과 바이트가 16 MB 를 초과하면 `Errc::TooLarge` 로 실패한다.
//
// @thread: any — 전역 setlocale() 에 절대 의존하지 않는 로케일 독립 구현.
// ─────────────────────────────────────────────────────────────────────────────
Result<std::string> Serialize(const Json& j);

// ─────────────────────────────────────────────────────────────────────────────
// SelfTest — [CCJ-7] 기동 시 1회 자가검증 (§3-1:169, §4-3:469-470)
//
// §3-1-a 골든 벡터 24개를 자가검사한다. 하나라도 불일치하면 기동을 즉시 실패시킨다.
//
// [소유 및 복제 원칙]
// 런타임 검사용 골든 벡터 24개는 `src/canonical_json.cpp` 내부에 상수로 내장되며,
// 단위 테스트 `tests/canonical_json_test.cpp` 는 동일한 기대값을 독립적으로 복제하여
// 이중 검증함으로써 순환 검증(circular validation) 오류를 방지한다.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] Error SelfTest();

}  // namespace cogito::ccj

#endif  // COGITO_CANONICAL_JSON_HPP
