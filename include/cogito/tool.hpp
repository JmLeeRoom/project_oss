// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 도구 계약 (descriptor · 결과 · 부수효과 등급)
//
// 규범 근거 : Cogito++_구현명세서.md §2(:68), §3-3(:260-267), §4-4(:522-593),
//             §4-12(:1150-1180), §6-2-a(:1585-1616), §7-5(:1951)
// G0 결정   : G0-25 (Accepted), G0-27 (Accepted), G0-29 (Accepted), ADR-0001 D1 [R3] (Accepted)
//
// 이 파일은 "설비에 무슨 일이 일어날 수 있는가"를 타입으로 고정한다.
// Effect·Risk·Idempotency 의 기본값은 전부 가장 위험한 쪽이다 — 선언을 빠뜨린
// descriptor 가 조용히 안전한 도구로 취급되는 경로를 남기지 않기 위해서다.
// handler 는 private 이며 ToolInvoker 만 꺼낼 수 있다(불변식 1을 타입으로 강제).
#ifndef COGITO_TOOL_HPP
#define COGITO_TOOL_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "cogito/canonical_json.hpp"
#include "cogito/result.hpp"
#include "cogito/tool_schema.hpp"   // GrammarCoverage — 기본값을 이름으로 쓰기 위해 필요하다

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// 전방 선언
//
// ToolCallContext 는 invoker.hpp 의 `struct ToolCallContext` 가 정의한다. 여기서 재정의하지 않는다.
//   ※ 명세 §4-4(:560)의 `const class ToolCallContext&` 는 elaborated-type-specifier 를
//     class 로 쓰지만 §4-12(:1160)·invoker.hpp 는 struct 로 정의한다. 클래스 키가
//     어긋나면 MSVC C4099 / clang -Wmismatched-tags 가 발생하므로 struct 로 통일한다.
//     (명세 §4-4:560 의 class 키를 struct 로 정정할 것 — 명세 반영 대상)
//
// GrammarCoverage 는 tool_schema.hpp 가 정의한다(§2 :67, §4-6 :671). 이제 그 헤더가 있으므로
// 불투명 선언이 아니라 정식 include 로 받는다 — 불투명 선언으로는 열거자를 이름으로 쓸 수 없어
// `grammar_coverage` 기본값이 값 초기화(0 = **Full**, 가장 관대한 값)로 떨어졌었다.
// ─────────────────────────────────────────────────────────────────────────────
struct ToolCallContext;
class ToolInvoker;

// ─────────────────────────────────────────────────────────────────────────────
// 부수효과 · 위험도 · 멱등성
//
// Effect 의 숫자값은 그냥 태그가 아니라 **권한 순서**다.
//   none(0) < write(1) < destructive(2)
// G0-29(⑧)가 ExecutionMode 를 숫자로 비교하지 않고 "허용 effect 상한"으로 사상한 뒤
// 최솟값을 취하도록 확정했는데, 그 min 이 잘 정의되는 근거가 이 선형 순서다.
// 값을 재배치하거나 중간에 새 값을 끼워 넣으면 상한 계산이 조용히 뒤집힌다.
//
// ※ ExecutionMode → max_effect 사상표와 결합 규칙은 여기 두지 않는다.
//   ExecutionMode 는 identity.hpp 담당이다(§2 :70). 이 헤더는 순서만 보장한다.
// ─────────────────────────────────────────────────────────────────────────────
enum class Effect      : std::uint8_t { None = 0, Write = 1, Destructive = 2 };
enum class Risk        : std::uint8_t { Low = 0, Medium = 1, High = 2, Critical = 3 };
enum class Idempotency : std::uint8_t { Safe, Conditional, Unsafe };

// Forbidden 은 삭제가 아니라 **tombstone** 이다(§4-5 :636, 결함 7).
// 불변식 2 — "미등록·tombstone 도구는 정책이 Allow 여도 Deny".
// 등록에서 지워버리면 Absent 와 구분되지 않아 "금지된 도구"와 "오타 난 도구"가
// 같은 사유로 거부되고, 게이트 2단계가 tool_forbidden 과 tool_not_registered 를
// 나눠 보고할 수 없게 된다.
enum class ToolStatus  : std::uint8_t { Enabled, Forbidden };

const char* ToString(Effect e) noexcept;
const char* ToString(Risk r) noexcept;
const char* ToString(Idempotency i) noexcept;
const char* ToString(ToolStatus s) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// 실행 결과 분류
//
// [취소·타임아웃 분류 — ADR-0001 D1 [R3]]
//   Execute 상태는 Cancel 을 FSM 이벤트로 받지 않는다. 실행 중 취소는 CancelToken 으로
//   Invoker 에 전달되고 결과가 여기로 분류된다.
//     effect == None  ->  ToolResultStatus::Cancelled        (깨끗한 취소)
//     effect != None  ->  ToolResultStatus::Indeterminate    (불변식 9)
//   둘 다 Event::ExecErrorOrIndeterminate 로 Observe 에 진입한다.
//   Observe 에서 CancelToken 이 여전히 set 이면 R2 로 Cancelled 로 간다.
//   ※ 쓰기 중 취소를 '깨끗한 취소'로 기록하면 불변식 9 가 무력화된다.
//
//   Timeout 도 같은 규칙을 따른다. deadline 초과 시 effect == None 이면 Timeout,
//   effect != None 이면 Timeout 이 아니라 Indeterminate 다 — 설비 상태를 알 수 없다
//   (invoker.hpp ToolInvoker::Invoke [실행 계약] 7).
// ─────────────────────────────────────────────────────────────────────────────
enum class ToolResultStatus : std::uint8_t {
  Ok, Error, Timeout, Cancelled, Indeterminate
};

const char* ToString(ToolResultStatus s) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// ModelToolDeclaration — LLM 모델에 노출되는 도구 선언 DTO
//
// 내부 핸들러, 위험도 등급, 프로바이더 메타데이터가 완전히 배제된 순수 인터페이스 DTO.
// ─────────────────────────────────────────────────────────────────────────────
struct ModelToolDeclaration {
  std::string name;
  std::string description;
  ccj::Json   parameters;   // input_schema 본문
};

// ─────────────────────────────────────────────────────────────────────────────
struct ToolResult {
  ToolResultStatus status = ToolResultStatus::Error;

  // status == Ok 일 때만 의미가 있다. 그 외 상태에서 content 를 읽지 않는다.
  ccj::Json    content;

  std::string  error_code;
  std::string  error_message;

  std::int64_t started_ns = 0, finished_ns = 0, elapsed_us = 0;

  // 불변식 9 — write/destructive 는 자동 재시도가 없으므로 항상 1 이다.
  // 1 보다 큰 값이 write 계열 결과에 나타나면 그 자체가 계약 위반이다.
  int          attempt_count = 1;

  // write 계열 전용. 요청 전/요청/요청 후 상태를 함께 남겨야 사후에
  // "설비가 실제로 바뀌었는가"를 판정할 수 있다.
  ccj::Json    before, requested, after;

  // "verified" | "mismatch" | "unavailable" (§4-4 :552).
  // unavailable 은 확인 실패가 아니라 '확인 수단이 없음'이다. 성공으로 읽지 않는다.
  std::string  verification_status;

  // 공통. §7-5(:1951) — max_output_bytes 절단 + 마스킹 후 플래그로 사실을 남긴다.
  // 절단·마스킹을 조용히 수행하고 플래그를 빠뜨리면 감사가 원문을 봤다고 오인한다.
  std::size_t  output_bytes = 0;
  bool         truncated = false;
  bool         masked = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// 도구 핸들러
//
// @thread: agent-loop-only — 호출은 ToolInvoker::Invoke 안에서만 일어나며,
//          AgentLoop owner thread 에서 직렬화된다(불변식 11).
//
// [계약]
//   - args 는 게이트 3단계에서 이미 input_schema 검증을 통과한 값이다.
//     그래도 handler 가 args 를 신뢰해 범위 검사를 생략해서는 안 된다 —
//     schema 는 정적 입력 범위만 강제하며 물리적 안전을 보장하지 않는다(부록 A).
//   - handler 는 ToolCallContext::cancel 과 deadline_ns 를 존중해야 한다.
//   - handler 가 던진 예외는 ToolInvoker 가 자체적으로 잡아
//     ToolResult{status=Error} 로 바꾼다(G0-09 규칙 3, invoker.hpp ToolInvoker::Invoke [실행 계약] 5).
//     예외가 밖으로 새면 §6-2-a 의 tool_result 커밋을 건너뛰고 FSM 이 Execute 에 남는다.
//   - handler 는 Subject 를 받지 않는다. 도구가 주체를 신뢰하게 만들지 않는다
//     (invoker.hpp ToolCallContext::subject 주석).
// ─────────────────────────────────────────────────────────────────────────────
using ToolHandler =
    std::function<ToolResult(const ccj::Json& args, const ToolCallContext&)>;

// ─────────────────────────────────────────────────────────────────────────────
class ToolDescriptor {
 public:
  // ^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*){0,4}$
  // 모델에 노출되는 식별자이자 Permit 결합 키다. 대문자·공백·유니코드를 받지 않는다.
  std::string      name;
  std::string      description;

  ccj::Json        input_schema;

  // null 허용. null 이어도 크기 상한과 JSON 파싱 제한은 그대로 적용된다(§4-4 :567).
  // "스키마가 없으니 검사도 없다"가 아니다.
  ccj::Json        output_schema;

  // 기본값은 전부 가장 위험한 쪽이다. 선언을 빠뜨린 descriptor 가
  // 승인 없이 실행되는 경로를 만들지 않는다.
  Effect           effect      = Effect::Destructive;
  Risk             risk        = Risk::Critical;
  Idempotency      idempotency = Idempotency::Unsafe;
  bool             approval_required = true;

  // 0 금지(§4-4 :572). 상한값은 명세에 없다 — 미결(제품 책임자 결정 필요).
  std::int32_t     timeout_ms        = 3000;

  // 결과 검증 1단계의 임계값. G0-27 순서는 아래 ValidateToolContract 주석 참조.
  std::size_t      max_output_bytes  = 64 * 1024;

  // 감사 귀속용. 형식·유일성 검증 규칙은 명세에 없다 — 미결(G0-32 참조).
  std::string      provider_id;
  std::string      invoker_id;

  ToolStatus       status = ToolStatus::Enabled;

  // status == Forbidden 일 때 필수. 게이트 2단계가 이 문자열을 그대로 사유로 쓴다
  // (§6-2 :1483). 비어 있으면 운영자가 왜 막혔는지 알 수 없다.
  std::string      forbidden_reason;

  // 스키마 컴파일러가 채운다(Freeze 시점). 도구 저자가 직접 쓰지 않는다.
  //
  // ⚠ 초기화자를 지우지 마라. §4-6(:671)의 열거 순서가 `{ Full, Partial, None }` 이라
  //   값 초기화(`{}` = 0)는 `Full` — **가장 관대한 값** — 이 된다. 명세 §4-4(:578)가 규정한
  //   기본값은 `None` 이므로, 초기화자가 없으면 헤더가 규범과 정반대 값을 낸다.
  //   `grammar_coverage` 가 조용히 `Full` 이면 §8-5 [A-3] 문법 커버리지 경고가 영원히 뜨지 않는다.
  //
  //   열거 순서를 `None = 0` 으로 뒤집어 다른 세 열거형(Effect·Risk·Idempotency)의
  //   '기본값은 가장 보수적으로' 규율에 맞출지 여부는 **제품 책임자 결정 필요**다(미결).
  //   순서를 바꾸면 감사 payload 에 이미 기록된 수치의 의미가 바뀐다 — tool_schema.hpp 참조.
  //
  //   어느 쪽이든 GBNF 적용 여부와 무관하게 arguments 는 항상 런타임 재검증된다
  //   (불변식 3). 이 필드는 보고용이며 검증을 건너뛰는 근거가 될 수 없다.
  GrammarCoverage  grammar_coverage = GrammarCoverage::None;

  // 불변식 1: handler 는 ToolInvoker 만 꺼낼 수 있다. 타입 수준에서 강제한다.
  // 공개 getter 를 만들지 마라 — 만드는 순간 Permit 을 우회하는 실행 경로가 생긴다.
  //
  // @thread: 부팅 단계 전용. Freeze() 이후 descriptor 를 수정하지 않는다.
  void SetHandler(ToolHandler h) { handler_ = std::move(h); }
  bool has_handler() const noexcept { return static_cast<bool>(handler_); }

 private:
  friend class ToolInvoker;
  ToolHandler handler_;
};

// ─────────────────────────────────────────────────────────────────────────────
// §3-3 effect × risk 하한 검사 및 계약 무결성 검증.
// 부팅 시 위반하면 **프로세스 시작 실패**이며 반환 Error 의 code 는 Errc::ToolContractViolation 이다.
//
// [Effect × Risk × Approval × Idempotency 전체 유효 행렬]
// ┌─────────────┬───────────────────────────┬───────────────────┬──────────────────────┐
// │ effect      │ 허용 risk                 │ approval_required │ 허용 idempotency     │
// ├─────────────┼───────────────────────────┼───────────────────┼──────────────────────┤
// │ none        │ low, medium, high, crit   │ false (기본) / true│ safe (기본), cond    │
// │ write       │ medium, high, critical    │ true (강제)*      │ conditional, unsafe  │
// │ destructive │ high, critical            │ true (강제)       │ unsafe (강제)        │
// └─────────────┴───────────────────────────┴───────────────────┴──────────────────────┘
// * 참고: Write 의 approval=false 면제는 런타임 정책·감사 엔진 연계 티켓에서 처리하므로,
//   본 티켓(S2 Registry)에서는 Write + approval=false 를 Errc::ToolContractViolation 으로 거부함.
//
// [필수 검증 규칙]
// 1. name: 길이 1~128 바이트, 정규식 ^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*){0,4}$ 준수.
// 2. timeout_ms: 1 <= timeout_ms <= 3600000 (1시간).
// 3. max_output_bytes: 1 <= max_output_bytes <= 10485760 (10MB).
// 4. provider_id, invoker_id: 길이 1~64 바이트, ^[a-z0-9_-]+$ 준수.
// 5. status == Enabled:
//    - input_schema 는 유효한 JSON 객체(is_object()).
//    - has_handler() 는 true (핸들러 필수).
//    - forbidden_reason 은 반드시 빈 문자열("").
// 6. status == Forbidden (Tombstone):
//    - forbidden_reason 은 비어있지 않아야 함(길이 >= 1).
//    - has_handler() 는 false (핸들러 금지).
// 7. enum 값 범위: Effect, Risk, Idempotency, ToolStatus 가 정의된 열거형 값 내에 있어야 함.
//
// @thread: 부팅 단계 전용.
[[nodiscard]] Error ValidateToolContract(const ToolDescriptor& d);

// ─────────────────────────────────────────────────────────────────────────────
// 이 헤더에 두지 않는 것 — 경계를 명시해 둔다
//
//   ToolRegistry · ToolProvider · LookupKind · LookupResult
//     → §2 저장소 레이아웃(:69)과 §4-5(:595-658)가 registry.hpp 담당으로 규정한다.
//       registry.hpp 는 아직 작성되지 않았다. invoker.hpp 의 전방 선언이 ToolRegistry 를
//       전방 선언만 하고 참조로 보관하므로 현재 전처리·컴파일은 막히지 않는다.
//       ※ 배정 지시에 등장한 `RegistryLookup` 이라는 이름은 명세에 없다.
//         실제 이름은 LookupResult / LookupKind 다(§4-5 :607-612). 지어내지 않는다.
//
//   SchemaCompiler · CompiledSchema · GrammarCoverage 정의
//     → tool_schema.hpp 담당(§2 :67, §4-6 :660-680).
//
//   ExecutionMode 와 mode → max_effect 사상표
//     → identity.hpp / 게이트 5단계 담당(§2 :70, G0-RESOLUTION-9 ⑧).
//
// ⚠ 미결 — G0-32(C ABI descriptor). 체크리스트 :104 · :965 가 미해소로 남긴 항목이다.
//   현재 C ABI 의 도구 등록 구조로는 output_schema · idempotency · max_output_bytes ·
//   provider_id / invoker_id 를 전달할 수 없고, v1.1 descriptor 의 struct/JSON 등록
//   계약과 `struct_size` 확장 규칙도 정의되어 있지 않다.
//   따라서 위 필드들이 C 경계를 어떻게 넘는지는 이 헤더가 정하지 않는다 —
//   제품 책임자 결정 필요. 임의의 ABI 표현을 여기에 선반영하지 않는다.
// ─────────────────────────────────────────────────────────────────────────────

}  // namespace cogito

#endif  // COGITO_TOOL_HPP
