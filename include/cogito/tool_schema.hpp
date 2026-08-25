// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 도구 인자 스키마 컴파일과 문법 커버리지
//
// 규범 근거 : Cogito++_구현명세서.md §2(레이아웃 :67), §4-6(:660-699), §3-2(Tier-G/Tier-V 화이트리스트)
// G0 결정   : G0-10(E-A Thompson NFA 엔진 및 스텝 예산 — Accepted), G0-27(스키마 컴파일 및 출력 검증 — Accepted)
//
// ★ 불변식 3 — "GBNF 적용 여부와 무관하게 arguments 를 **항상** 런타임 재검증한다."
//   이 헤더의 어떤 것도 재검증을 건너뛰는 근거가 되지 않는다. `GrammarCoverage::Full` 조차
//   "문법이 Tier-G 키워드를 전부 덮었다"는 보고일 뿐이며 스키마 위반을 불가능하게 만들지 않는다.
#ifndef COGITO_TOOL_SCHEMA_HPP
#define COGITO_TOOL_SCHEMA_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cogito/canonical_json.hpp"
#include "cogito/result.hpp"

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// 패턴 파싱 및 정규식 실행 예산 상한 (G0-10 E-A Thompson NFA 확정)
//
// [스텝 계산 및 결정론적 예산 규약]
// - NFA 는 바이트 단위(UTF-8 code unit)로 시뮬레이션한다.
// - 1 Consuming Step: 입력 바이트 1개를 소비할 때, 현재 활성 상태 집합(active states) 내의
//   각 상태에 대해 전이 조건(predicate: 바이트 리터럴, 문자 클래스, '.' 등)을 검사한다.
//   검사 대상 활성 상태 1개당 1 스텝을 소비한다 (일치 및 불일치 탈락 모두 상태당 1 스텝 회계).
// - 1 Epsilon Step: ε-전이(분기 '|', 수량자 '*', '+', '?', 그룹 진입/퇴출) 확장 시
//   방문하는 ε-전이 간선(edge) 1개당 1 스텝을 소비한다.
//   동일 ε-클로저 탐색 패스 내에서 이미 방문한 상태에 재진입을 시도하는 경우에도
//   방문 시도 1회당 1 스텝을 소비하고 중복 탐색은 가지치기(pruning)한다.
// - 초기 및 종료 시점 ε-클로저:
//   * 입력 바이트 소비 전 시작 상태(s0)로부터의 초기 ε-클로저 확장도 간선당 1 스텝을 소비한다.
//   * 각 바이트 소비 직후의 ε-클로저 확장도 간선당 1 스텝을 소비한다.
//   * 모든 입력 바이트 소비 후 최종 활성 상태 집합 내 수락 상태(s_accept) 존재 확인은 추가 스텝 미소비.
// - 멀티바이트 UTF-8 및 문자 클래스:
//   * 다중 바이트 UTF-8 리터럴은 바이트 단위의 선형 NFA 전이 체인으로 컴파일되어 바이트당 전이·스텝 소비.
//   * 긍정/부정 문자 클래스([...], [^...]): 바이트 단위 predicate 로 활성 상태 검사 시 1 스텝 소비.
//   * '.' 메타 문자: \n(0x0A) 및 \r(0x0D)을 제외한 유효한 Unicode scalar 1개(UTF-8 1~4바이트)를 순차 소비하며,
//     소비되는 바이트마다 1 consuming step 부과 (예: '^.$' 는 'a'(1B, 1스텝) 및 '한'(3B, 3스텝) 모두 Match 성공).
//   * 잘못된 UTF-8 바이트 시퀀스 감지 시 즉시 Errc::SchemaViolation 반환.
// - 예산 범위: 단일 pattern 검증 1회 호출당(per-evaluation) kPatternMatchStepBudget 스텝.
// - 경계 판정:
//     * step_count <= step_budget 시점에 입력 종료 및 수락(accept) 상태 도달 시 -> 성공(Match)
//     * step_count <= step_budget 시점에 입력 종료 및 수락 상태 미도달 시 -> 실패(NoMatch)
//     * step_count >= step_budget 시점에 추가 전이가 필요한데 수락에 미도달한 경우 -> Errc::PatternBudgetExhausted
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr std::size_t   kMaxPatternBytes        = 256;     // 패턴 바이트 상한
inline constexpr std::size_t   kMaxQuantifierBound     = 1024;    // {n,m} 에서 m 상한 (n <= m <= 1024)
inline constexpr std::size_t   kMaxAlternationProduct  = 256;     // 교대 분기 곱 상한 (Π |alt_i| <= 256)
inline constexpr std::size_t   kMaxMatchStringBytes    = 65536;   // 매칭 대상 문자열 길이 상한
inline constexpr std::uint64_t kPatternMatchStepBudget = 100'000; // 결정론적 정규식 매칭 스텝 예산
inline constexpr std::size_t   kMaxDiagnosticBytes     = 512;     // 진단 메시지 최대 바이트

// ─────────────────────────────────────────────────────────────────────────────
// GrammarCoverage — 제약 디코딩(GBNF)이 스키마를 얼마나 덮었는지에 대한 **보고값**
//
// [산출 규칙 — §3-2:236-239]
// - Tier-V 키워드가 1개 이상 포함된 경우                         -> Partial
// - Tier-V 가 전혀 없고 실질적 제약(enum, min/maxLength, min/maximum, pattern)이 있음 -> Full
// - 제약 키워드가 하나도 없는 경우(type/properties/required 만 있는 경우 포함)        -> None
// ─────────────────────────────────────────────────────────────────────────────
enum class GrammarCoverage : std::uint8_t {
  Full = 0,
  Partial = 1,
  None = 2
};

// "full" | "partial" | "none". 감사·UI 가 이 문자열로 분기한다(§4-6:673).
const char* ToString(GrammarCoverage c) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// SchemaAudit — 왜 Partial 인지를 감사에 남긴다.
// ─────────────────────────────────────────────────────────────────────────────
struct SchemaAudit {
  GrammarCoverage coverage = GrammarCoverage::None;

  // Tier-V(문법으로 표현 불가) 소문자 키워드 목록 (예: "type:number", "number_bounds", "const")
  std::vector<std::string> tier_v_keywords;
};

// ─────────────────────────────────────────────────────────────────────────────
// CompiledSchema — 컴파일된 불변 검증기
//
// json-schema-validator 를 헤더에서 숨긴다 (Pimpl).
// ─────────────────────────────────────────────────────────────────────────────
class CompiledSchema {
 public:
  ~CompiledSchema();

  // "" 이면 유효성 검증 성공. 실패 시 최대 512바이트 진단 문자열 반환 (초과 시 "..." 절단).
  // 패턴 매칭 예산 초과 시 "pattern_budget_exhausted" 반환.
  std::string Check(const ccj::Json& doc) const;

  // 오류 객체 기반 검증 함수 (Check 기반 fail-closed 래퍼)
  // - 유효: Error::Ok()
  // - 불일치: Errc::SchemaViolation (reason: reason::kSchemaViolation)
  // - 예산 소진: Errc::PatternBudgetExhausted (reason: reason::kPatternTimeout)
  Error Validate(const ccj::Json& doc) const;

  const SchemaAudit& audit() const noexcept;

 private:
  friend class SchemaCompiler;
  CompiledSchema();

  struct Impl;
  std::unique_ptr<Impl> p_;
};

// ─────────────────────────────────────────────────────────────────────────────
// SchemaCompiler — 부팅 단계에서만 쓴다.
//
// [컴파일 순서 및 필수 정적 제약 — §3-2-a, §4-6]
//   1. Draft-7 화이트리스트 검사 (금지 키워드 $ref, allOf, anyOf, oneOf 등 거부 -> SchemaCompileFailed)
//   2. pattern 복잡도 및 정적 제약 검사:
//      (a) 반드시 ^ 로 시작하고 $ 로 끝남 (위반 시 SchemaCompileFailed)
//      (b) 패턴 길이 <= kMaxPatternBytes(256B) (초과 시 SchemaCompileFailed)
//      (c) 수량자 {n,m} 에서 n <= m <= kMaxQuantifierBound(1024) (초과 시 SchemaCompileFailed)
//      (d) 교대 분기 곱 = Π |alt_i| <= kMaxAlternationProduct(256) (초과 시 SchemaCompileFailed)
//      (e) 금지 요소: 중첩 수량자, 백레퍼런스, 룩어라운드, 그룹 뒤 *, + 금지 (발견 시 SchemaCompileFailed)
//      (f) maxLength 동반 필수: pattern 을 가진 string 노드는 반드시 동일 객체에 maxLength (<= 65536)를
//          함께 선언해야 함 (누락 또는 65536 초과 시 SchemaCompileFailed)
//   3. validator 컴파일 (문법/형식 오류 -> SchemaCompileFailed)
//   4. grammar_coverage 산출 (Full/Partial/None 판정)
//
// 하나라도 실패하면 Result<std::unique_ptr<CompiledSchema>> 실패(Errc::SchemaCompileFailed) 반환.
// ─────────────────────────────────────────────────────────────────────────────
class SchemaCompiler {
 public:
  // @thread: 부팅 단계 전용. Freeze() 이후 호출하지 않는다.
  static Result<std::unique_ptr<CompiledSchema>> Compile(const ccj::Json& schema);
};

// ─────────────────────────────────────────────────────────────────────────────
// MatcherTestSeam — G0 E5 재난적 정규식(Catastrophic) 직접 주입 검증용 테스트 Seam
//
// [정적 검사 우회 범위 및 반환 규약]
// - 우회 대상 5종 (명시적 목록):
//   1. kMaxPatternBytes (256 바이트 패턴 길이 상한)
//   2. kMaxQuantifierBound (1024 수량자 상한)
//   3. kMaxAlternationProduct (256 교대 분기 곱 상한)
//   4. ^...$ 앵커 필수 검사 (앵커 미포함 패턴 허용)
//   5. maxLength 동반 요구 검사 (단독 패턴 주입)
// - Result<bool> 반환값 매핑:
//   * Match 성공: Result<bool>::Ok(true)
//   * Match 실패(불일치): Result<bool>::Ok(false)
//   * 문법 오류 (NFA 파싱 불가 문법): Errc::SchemaCompileFailed
//   * 잘못된 UTF-8 또는 문자열 길이 초과(> 65536): Errc::SchemaViolation
//   * step_budget == 0 또는 실행 도중 스텝 예산 소진: Errc::PatternBudgetExhausted
// ─────────────────────────────────────────────────────────────────────────────
namespace testing {
struct MatcherTestSeam {
  static Result<bool> DirectNfaMatch(std::string_view raw_pattern,
                                     std::string_view input_text,
                                     std::uint64_t step_budget = kPatternMatchStepBudget);
};
}  // namespace testing

}  // namespace cogito

#endif  // COGITO_TOOL_SCHEMA_HPP
