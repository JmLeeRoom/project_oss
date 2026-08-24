// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 도구 인자 스키마 컴파일과 문법 커버리지
//
// 규범 근거 : Cogito++_구현명세서.md §2(레이아웃 :67), §4-6(:660-699), §3-2(Tier-G/Tier-V 화이트리스트)
// G0 결정   : G0-10(pattern 타임아웃 — docs/g0/G0-10-regex-timeout.md, Proposed),
//             G0-27(output schema 검증 시점 — 미해소)
//
// ★ 불변식 3 — "GBNF 적용 여부와 무관하게 arguments 를 **항상** 런타임 재검증한다."
//   이 헤더의 어떤 것도 재검증을 건너뛰는 근거가 되지 않는다. `GrammarCoverage::Full` 조차
//   "문법이 Tier-G 키워드를 전부 덮었다"는 보고일 뿐이며 스키마 위반을 불가능하게 만들지 않는다.
//   (명세 부록 A 가 "GBNF 가 스키마 위반을 구조적으로 불가능하게 한다"를 금지 표현으로 명시)
//
// ⚠ 선반영 고지 — 이 파일은 G0-RESOLUTION-9 의 **Proposed** 결정 및 미해소 G0(G0-10·G0-27)와
//    맞물린 초안이며 승인 전에는 규범이 아니다. 승인 전까지 이 헤더를 구현 기준으로 인계하지 않는다.
//    승인 시 이 고지를 제거한다. (docs/STATUS-AUDIT-2026-08-24.md §2-⑥)
#ifndef COGITO_TOOL_SCHEMA_HPP
#define COGITO_TOOL_SCHEMA_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cogito/canonical_json.hpp"
#include "cogito/result.hpp"

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// GrammarCoverage — 제약 디코딩(GBNF)이 스키마를 얼마나 덮었는지에 대한 **보고값**
//
// ⚠ 열거 순서 주의 — §4-6(:671) 이 `{ Full, Partial, None }` 으로 규정하므로
//   값 초기화(`{}` = 0)는 `Full`, 즉 **가장 관대한 값**이 된다.
//   이 프로젝트의 다른 세 열거형은 정반대 규율을 따른다:
//     Effect      기본 Destructive   (tool.hpp — 가장 위험하게)
//     Risk        기본 최고 등급
//     Idempotency 기본 비멱등
//   따라서 이 타입을 담는 필드는 **반드시 명시적으로 `= GrammarCoverage::None` 으로 초기화**한다.
//   `{}` 나 값 초기화에 기대면 조용히 `Full` 이 되어 [A-3] 경고가 영원히 뜨지 않는다.
//   §4-4(:578) `ToolDescriptor::grammar_coverage` 와 아래 `SchemaAudit::coverage` 가 그 예다.
//
//   열거 순서를 `None = 0` 으로 뒤집을지 여부는 **제품 책임자 결정 필요**다(미결).
//   순서를 바꾸면 감사 payload 에 이미 기록된 수치의 의미가 바뀌므로 되돌리기 어렵다.
// ─────────────────────────────────────────────────────────────────────────────
enum class GrammarCoverage : std::uint8_t { Full, Partial, None };

// "full" | "partial" | "none". 감사·UI 가 이 문자열로 분기한다(§4-6:673).
const char* ToString(GrammarCoverage c) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// SchemaAudit — 왜 Partial 인지를 감사에 남긴다.
// 이유 없는 Partial 은 사후 조사에서 "문법이 왜 못 덮었는지"를 알 수 없게 만든다.
// ─────────────────────────────────────────────────────────────────────────────
struct SchemaAudit {
  GrammarCoverage coverage = GrammarCoverage::None;   // 명시 초기화 — 위 주의 참조

  // Tier-V(문법으로 표현 불가) 키워드 목록. partial 사유를 감사에 남긴다(§4-6:677).
  std::vector<std::string> tier_v_keywords;
};

// ─────────────────────────────────────────────────────────────────────────────
// CompiledSchema — 컴파일된 검증기.
// json-schema-validator 를 헤더에서 숨긴다(pimpl) — 공개 헤더가 서드파티 타입을
// 노출하면 소비자가 그 라이브러리에 함께 묶인다(§4-6:687 주석).
// ─────────────────────────────────────────────────────────────────────────────
class CompiledSchema {
 public:
  ~CompiledSchema();

  // "" 이면 통과. 실패 시 "/properties/value: 3.5 exceeds maximum 0.95" 형태(§4-6:682).
  //
  // ⚠ 반환 문자열은 **입력 값의 일부를 포함한다.** 감사 payload 나 UI 권위 영역에 넣기 전에
  //   §7-5 마스킹을 거쳐야 한다(불변식 10 — 도구·LLM 유래 문자열은 신뢰하지 않는다).
  //
  // ⚠ pattern 키워드의 런타임 상한(§3-2, `reason::kPatternTimeout`)은 **G0-10 미해소**다.
  //   상한 초과 시 이 함수가 어떻게 신호하는지는 G0-10 확정 전까지 미결이다.
  //   docs/g0/G0-10-regex-timeout.md (Proposed) 참조.
  std::string Check(const ccj::Json& doc) const;

  const SchemaAudit& audit() const noexcept;

 private:
  friend class SchemaCompiler;
  CompiledSchema();

  struct Impl;                       // json-schema-validator 를 헤더에서 숨긴다
  std::unique_ptr<Impl> p_;
};

// ─────────────────────────────────────────────────────────────────────────────
// SchemaCompiler — 부팅 단계에서만 쓴다.
//
// [컴파일 순서 — §4-6:693-694]
//   §3-2 화이트리스트 검사  ->  pattern 복잡도 검사  ->  validator 컴파일
//   ->  grammar_coverage 산출.  **하나라도 실패하면 Error** (fail-closed, 불변식 4).
//
// 실패를 무시하고 "검증 없이 통과"로 강등하지 않는다 — 그 순간 불변식 3 이 무의미해진다.
// ─────────────────────────────────────────────────────────────────────────────
class SchemaCompiler {
 public:
  // @thread: 부팅 단계 전용. Freeze() 이후 호출하지 않는다.
  static Result<std::unique_ptr<CompiledSchema>> Compile(const ccj::Json& schema);
};

}  // namespace cogito

#endif  // COGITO_TOOL_SCHEMA_HPP
