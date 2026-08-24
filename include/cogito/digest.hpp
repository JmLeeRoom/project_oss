// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 다이제스트 계약 및 도메인 태그 프로젝션 (CCJ v1 / LP 인코딩)
//
// 규범 근거 : Cogito++_구현명세서.md §4-3(:445-473), §6-1(:1401-1440)
// G0 결정   : G0-05 (Accepted — operation_digest 신설),
//             G0-23 (Accepted — CCJ v1 정규 직렬화),
//             G0-26 (Accepted — 도메인 태그 9종 및 projection 표),
//             ADR-0004 D1·D4·D5·D6·D7 (Accepted)
//
// [도메인 분리 및 무결성 원칙]
// - 모든 digest 는 고유한 도메인 태그(domain tag)로 시작하며 Length-Prefixed(LP) 인코딩으로 결합된다.
// - OpenSSL EVP 연산 실패 및 CCJ 정규화 실패는 모두 fail-closed 방식으로 호출자에게 Result<Digest>로 전달된다.
//
// [LP 인코딩 규약 (ADR-0004 D5 / G0-26 확정)]
// H(f₁ … fₙ) = SHA-256( Σᵢ [ field_bytes(fᵢ) ] )
//   - 첫 필드는 항상 도메인 태그: u32le(len) ‖ UTF-8 태그 바이트
//   - 문자열/바이트: u32le(len) ‖ UTF-8 원문 바이트
//   - U64 정수: 원시 u64le 8바이트 (별도 길이 접두사 없음, 십진 문자열 금지)
//   - Digest: 원시 32바이트 (별도 길이 접두사 없음, hex 문자열 금지)
//   - JSON 객체: CCJ 정규 직렬화 후 u32le(len) ‖ CCJ 바이트
//   - 누락/optional 필드: 항상 빈 문자열 "" (u32le(0)) 로 포함하여 슬롯 개수 유지
#ifndef COGITO_DIGEST_HPP
#define COGITO_DIGEST_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cogito/canonical_json.hpp"
#include "cogito/ids.hpp"
#include "cogito/result.hpp"

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// 도메인 태그 9종 (G0-26 / ADR-0004 D6 확정 — 되돌림 불가)
// ─────────────────────────────────────────────────────────────────────────────
namespace domain {
inline constexpr std::string_view kAction     = "cogito-action-v1";
inline constexpr std::string_view kOperation  = "cogito-operation-v1";
inline constexpr std::string_view kPermit     = "cogito-permit-v1";
inline constexpr std::string_view kAudit      = "cogito-audit-v1";
inline constexpr std::string_view kPolicy     = "cogito-policy-v1";
inline constexpr std::string_view kRegistry   = "cogito-registry-v1";
inline constexpr std::string_view kToolSchema = "cogito-toolschema-v1";
inline constexpr std::string_view kConfig     = "cogito-config-v1";
inline constexpr std::string_view kModel      = "cogito-model-v1";
}  // namespace domain

// ─────────────────────────────────────────────────────────────────────────────
// SHA-256 기본 암호화 함수 (fail-closed API)
// ─────────────────────────────────────────────────────────────────────────────
Result<Digest> Sha256(const void* data, std::size_t len);

inline Result<Digest> Sha256(std::string_view sv) {
  return Sha256(sv.data(), sv.size());
}

inline Result<Digest> Sha256(const std::vector<std::uint8_t>& bytes) {
  return Sha256(bytes.data(), bytes.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// LpBuffer — Length-Prefixed 직렬화 버퍼 (ADR-0004 D5)
//
// [도메인 우선 강제 및 Sticky Error]
// - Create(domain_tag) 팩토리를 통해서만 인스턴스를 생성할 수 있어 도메인 태그 선행을 강제한다.
// - 한 번이라도 Append 연산이 실패하면 sticky error 상태가 되며, ComputeDigest() 호출 시
//   그 오류를 즉시 반환한다 (fail-closed).
// ─────────────────────────────────────────────────────────────────────────────
class LpBuffer {
 public:
  // 정적 팩토리: 첫 필드로 도메인 태그를 인코딩하며 시작
  static Result<LpBuffer> Create(std::string_view domain_tag);

  // 문자열/바이트 필드: u32le(길이) ‖ UTF-8 원본 바이트
  Error AppendString(std::string_view str);
  Error AppendBytes(const void* data, std::size_t size);

  // 정수 필드: u64le 고정 8바이트 (길이 접두사 없음, 십진 문자열 금지)
  Error AppendU64(std::uint64_t value);

  // Digest 필드: 원시 32바이트 (길이 접두사 없음, hex 문자열 금지)
  Error AppendDigest(const Digest& digest);

  // JSON 객체 필드: CCJ 정규 직렬화 후 u32le(길이) ‖ CCJ 바이트열
  // (CCJ 직렬화 실패 시 sticky error 설정 및 오류 반환)
  Error AppendJson(const ccj::Json& json);

  // 누적된 원시 바이트열 반환 (오류 상태일 경우 비어있을 수 있음)
  const std::vector<std::uint8_t>& bytes() const noexcept { return buffer_; }

  // 최종 SHA-256 다이제스트 계산 (sticky error 발생 시 해당 Error 반환)
  Result<Digest> ComputeDigest() const;

  bool ok() const noexcept { return last_error_.ok(); }
  Error last_error() const noexcept { return last_error_; }

 private:
  LpBuffer() = default;

  std::vector<std::uint8_t> buffer_;
  Error last_error_{Error::Ok()};
};

// ─────────────────────────────────────────────────────────────────────────────
// 프로젝션 DTO 구조체 (Projection DTOs)
//
// 핵심 로직/포인터/핸들러 주소/비밀값이 다이제스트에 유출되지 않도록 고정된 DTO를 사용한다.
// ─────────────────────────────────────────────────────────────────────────────

// 도구 레지스트리 프로젝션 DTO (Registry Digest 산출용)
struct ToolProjectionDto {
  std::string   name;                          // 도구 고유 식별자 (필수)
  std::string   status = "enabled";            // "enabled" | "forbidden" (전체 소문자)
  std::string   forbidden_reason;              // status == "forbidden" 일 때 사유, enabled 시 ""
  Digest        toolschema_digest;             // 해당 도구의 스키마 다이제스트 (32바이트)
  std::string   grammar_coverage = "none";     // "none" | "partial" | "full"
  std::string   effect = "none";               // "none" | "write" | "destructive"
  std::string   risk = "low";                  // "low" | "medium" | "high" | "critical"
  std::string   idempotency = "safe";          // "safe" | "conditional" | "unsafe"
  std::uint64_t approval_required = 0;         // 승인 필요 여부 (1 또는 0)
  std::uint64_t timeout_ms = 0;                // 실행 제한시간 (ms)
  std::uint64_t max_output_bytes = 0;          // 출력 상한 바이트 수
  std::string   provider_id;                   // 프로바이더 식별자 (생략 시 "")
  std::string   invoker_id;                    // 인보커 식별자 (생략 시 "")
};

// 보안 정책 규칙 프로젝션 DTO (Policy Digest 산출용)
struct PolicyRuleProjectionDto {
  std::uint64_t priority = 0;                  // 정렬 1순위: 내림차순 (높은 우선순위 먼저)
  std::string   rule_id;                       // 정렬 2순위: 오름차순 (동률 시 사전순)
  ccj::Json     normalized_rule;               // 비밀값/주석이 제거된 정규화된 규칙 본문
};

// ─────────────────────────────────────────────────────────────────────────────
// 9대 도메인 프로젝션 함수 계약 (G0-26 / ADR-0004 확정)
//
// 9개 함수 모두 Result<Digest> 를 반환하며 실패 시 즉시 에러 코드를 반환한다.
// ─────────────────────────────────────────────────────────────────────────────

// ① Action Digest (턴 및 실행 인스턴스에 결합)
// 필드: tag, session_id, turn_id(u64), action_id, tool_name, CCJ(arguments)
Result<Digest> ComputeActionDigest(const SessionId& session_id,
                                   TurnId turn_id,
                                   const ActionId& action_id,
                                   const std::string& tool_name,
                                   const ccj::Json& arguments);

// ② Operation Digest (G0-05: 턴 무관 도구+인자 순수 잠금 키)
// 필드: tag, tool_name, CCJ(arguments)
Result<Digest> ComputeOperationDigest(const std::string& tool_name,
                                     const ccj::Json& arguments);

// ③ Permit Digest (실행 허가 범위 스냅샷)
// 필드: tag, action_digest(32), subject_id, mode(u64), policy_digest(32), registry_digest(32)
Result<Digest> ComputePermitDigest(const Digest& action_digest,
                                    const std::string& subject_id,
                                    std::uint64_t mode,
                                    const Digest& policy_digest,
                                    const Digest& registry_digest);

// ④ Audit Digest (감사 체인 링크 해시)
// 필드: tag, prev_hash(32), event_id, session_id, turn_id(u64), action_id, wall_time_utc,
//       monotonic_ns(u64), process_epoch_id, kind, actor_type(u64), actor_id,
//       CCJ(payload), schema_version(u64)
Result<Digest> ComputeAuditDigest(const Digest& prev_hash,
                                  const std::string& event_id,
                                  const SessionId& session_id,
                                  TurnId turn_id,
                                  const ActionId& action_id,
                                  const std::string& wall_time_utc,
                                  std::uint64_t monotonic_ns,
                                  const std::string& process_epoch_id,
                                  const std::string& kind,
                                  std::uint64_t actor_type,
                                  const std::string& actor_id,
                                  const ccj::Json& payload,
                                  std::uint64_t schema_version);

// ⑤ ToolSchema Digest (단일 도구 입출력 스키마)
// 필드: tag, name, CCJ(input_schema), CCJ(output_schema 또는 null)
Result<Digest> ComputeToolSchemaDigest(const std::string& name,
                                       const ccj::Json& input_schema,
                                       const ccj::Json& output_schema);

// ⑥ Registry Digest (도구 레지스트리 전체 스냅샷)
// 정렬 규칙: name UTF-8 바이트 오름차순
// 중복 규칙: 동일한 name 발견 시 Errc::DuplicateKey 로 실패
// 필드: tag, Σ_name_asc [ name, status, forbidden_reason, toolschema_digest(32),
//                         grammar_coverage, effect, risk, idempotency,
//                         approval_required(u64), timeout_ms(u64), max_output_bytes(u64),
//                         provider_id, invoker_id ]
Result<Digest> ComputeRegistryDigest(std::vector<ToolProjectionDto> tools);

// ⑦ Policy Digest (보안 정책 스냅샷)
// 정렬 규칙: priority 내림차순 -> rule_id UTF-8 바이트 오름차순
// 중복 규칙: 동일한 rule_id 발견 시 Errc::DuplicateKey 로 실패
// 필드: tag, schema_version(u64), default_decision,
//       Σ_sorted [ priority(u64), rule_id, CCJ(normalized_rule) ]
Result<Digest> ComputePolicyDigest(std::uint64_t schema_version,
                                   const std::string& default_decision,
                                   std::vector<PolicyRuleProjectionDto> rules);

// ⑧ Config Digest (설정 스냅샷)
// 규약: 호출자는 비밀값 원문과 절대 경로가 제거된 normalized_config 를 전달해야 함
// 필드: tag, schema_version(u64), CCJ(normalized_config)
Result<Digest> ComputeConfigDigest(std::uint64_t schema_version,
                                   const ccj::Json& normalized_config);

// ⑨ Model Digest (모델·가중치·템플릿 스냅샷)
// 규약: 엔드포인트 URL 및 API 키 등 인증 비밀값은 완전히 배제
// 필드: tag, provider_id, model_id, weights_sha256, chat_template_digest, tokenizer_digest, quantization
Result<Digest> ComputeModelDigest(const std::string& provider_id,
                                  const std::string& model_id,
                                  const std::string& weights_sha256,
                                  const std::string& chat_template_digest,
                                  const std::string& tokenizer_digest,
                                  const std::string& quantization);

}  // namespace cogito

#endif  // COGITO_DIGEST_HPP
