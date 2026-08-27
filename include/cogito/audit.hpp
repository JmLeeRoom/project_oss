// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 감사 저널 (Audit Journal & Hash Chain)
//
// 규범 근거 : Cogito++_구현명세서.md §7-4(:1918-1975), §7-5(:1976-1986), §6-2-a(:1617-1645),
//             §3 불변식 2·4·7·8·10, 체크리스트 S5-05 ~ S5-10
// G0 결정   : G0-06 (pending_turn_end 멱등 재커밋), G0-21 (schema migration 001)
// ADR 결정  : ADR-0004 D5·D6·D8·D9 (Accepted)
//
// [불변 감사 원칙]
// 1. 단일 작가 원칙 (Single Writer): 감사 해시체인은 분기(branch)가 없어야 한다.
// 2. 물리적 Append-Only: UPDATE/DELETE 는 trigger + SQLite authorizer 두 겹으로 차단된다.
// 3. 감사 선행 불변식: Audit Commit -> FSM State Transition -> Tool Execution 순서를 엄수한다.
//    감사 커밋 실패 시 도구 핸들러 호출 횟수는 반드시 0이어야 한다 (불변식 8).
#ifndef COGITO_AUDIT_HPP
#define COGITO_AUDIT_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cogito/canonical_json.hpp"
#include "cogito/digest.hpp"
#include "cogito/ids.hpp"
#include "cogito/result.hpp"

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// ActorType — 감사 이벤트 주체 분류
// ─────────────────────────────────────────────────────────────────────────────
enum class ActorType : std::uint64_t {
  Core     = 1,   // Cogito++ Core 엔진 (Gate, FSM, Loop)
  User     = 2,   // 대화 입력 사용자 (End User)
  Operator = 3,   // 운영자 / 보안 관리자 (Human Approver)
  System   = 4,   // 시스템 / 환경 (Clock, OS, Secret Backend)
  Tool     = 5,   // 도구 실행 인스턴스 (Tool Handler / Provider)
};

inline constexpr std::string_view ToString(ActorType type) noexcept {
  switch (type) {
    case ActorType::Core:     return "core";
    case ActorType::User:     return "user";
    case ActorType::Operator: return "operator";
    case ActorType::System:   return "system";
    case ActorType::Tool:     return "tool";
  }
  return "unknown";
}

inline Result<ActorType> ParseActorType(std::string_view sv) noexcept {
  if (sv == "core")     return ActorType::Core;
  if (sv == "user")     return ActorType::User;
  if (sv == "operator") return ActorType::Operator;
  if (sv == "system")   return ActorType::System;
  if (sv == "tool")     return ActorType::Tool;
  return Error{Errc::InvalidArgument, "", "Unknown actor type string"};
}

// ─────────────────────────────────────────────────────────────────────────────
// EventKind — 감사 이벤트 유형 규범 상수 (§7-4)
// ─────────────────────────────────────────────────────────────────────────────
namespace event_kind {
inline constexpr const char* kTurnBegin         = "turn_begin";
inline constexpr const char* kInferBegin        = "infer_begin";
inline constexpr const char* kInferEnd          = "infer_end";
inline constexpr const char* kGateVerdict       = "gate_verdict";
inline constexpr const char* kApprovalRequested = "approval_requested";
inline constexpr const char* kApprovalGranted   = "approval_granted";
inline constexpr const char* kApprovalRejected  = "approval_rejected";
inline constexpr const char* kToolCallStarted   = "tool_call_started";
inline constexpr const char* kToolResult        = "tool_result";
inline constexpr const char* kTurnEnd           = "turn_end";
inline constexpr const char* kAuditRecovery     = "audit_recovery";
}  // namespace event_kind

// ─────────────────────────────────────────────────────────────────────────────
// AuditEvent — 단일 감사 레코드 DTO
//
// 해시 계산 시 `seq` 자체는 DB 채번값이므로 해시 입력에 포함되지 않는다(ADR-0004 D9).
// 무결성은 `prev_hash` 체인이 증명한다.
// ─────────────────────────────────────────────────────────────────────────────
struct AuditEvent {
  std::uint64_t seq = 0;              // 전역 순서 권위 (DB AUTOINCREMENT, 커밋 전 0)
  std::string   event_id;             // 이벤트 고유 ID (UUIDv4)
  SessionId     session_id;           // 세션 ID (UUIDv4)
  TurnId        turn_id = 0;          // 턴 ID (1부터 단조 증가)
  ActionId      action_id;            // 액션 ID (선택적, 턴 수준 이벤트는 비어있음)
  std::string   wall_time_utc;        // RFC3339 UTC 시각 (표시·상관관계 전용)
  std::int64_t  monotonic_ns = 0;     // 단조 시각 ns (프로세스 에포크 내 순서)
  std::string   process_epoch_id;     // 프로세스 기동 에포크 UUIDv4
  std::string   kind;                 // event_kind::* 상수 문자열
  ActorType     actor_type = ActorType::Core; // 주체 구분
  std::string   actor_id;             // 주체 식별자
  ccj::Json     payload;              // CCJ v1 정규 JSON 페이로드 (마스킹 완료)
  std::uint64_t schema_version = 1;   // 감사 스키마 버전 (1)
  Digest        prev_hash;            // 이전 이벤트의 32바이트 SHA-256 해시
  Digest        hash;                 // 현재 이벤트의 32바이트 SHA-256 해시
};

// ─────────────────────────────────────────────────────────────────────────────
// AuditJournal — 감사 저널 추상 인터페이스
// ─────────────────────────────────────────────────────────────────────────────
class AuditJournal {
 public:
  virtual ~AuditJournal() = default;

  // 이벤트 원자적 커밋 (prev_hash 연결, 해시 계산, DDL 삽입)
  // 반환값: 할당된 seq 번호
  virtual Result<std::uint64_t> Commit(AuditEvent event) = 0;

  // 현재 체인의 최신 헤드 해시 및 시퀀스 조회
  virtual Result<Digest> GetHeadHash() const = 0;
  virtual Result<std::uint64_t> GetHeadSeq() const = 0;

  // 세션별 이벤트 조회
  virtual Result<std::vector<AuditEvent>> QuerySession(const SessionId& session_id,
                                                       std::uint64_t from_seq = 0,
                                                       std::size_t limit = 100) const = 0;

  // 전체 체인 무결성 검증 (순서, 해시 체인, CCJ 형식 전수 재계산)
  virtual Result<bool> VerifyChain() const = 0;

  // 크래시 복구: dangling tool_call_started 를 indeterminate tool_result 로 안전 복구
  virtual Result<std::size_t> RecoverDangling() = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// RecordingAuditJournal — 인메모리 감사 저널 (테스트 Seam & Mock)
//
// 실제 SQLite 파일 없이 단위/통합 테스트에서 감사 이벤트 발생 순서와 실패 주입을 검증한다.
// ─────────────────────────────────────────────────────────────────────────────
class RecordingAuditJournal : public AuditJournal {
 public:
  RecordingAuditJournal() = default;

  Result<std::uint64_t> Commit(AuditEvent event) override;
  Result<Digest> GetHeadHash() const override;
  Result<std::uint64_t> GetHeadSeq() const override;
  Result<std::vector<AuditEvent>> QuerySession(const SessionId& session_id,
                                               std::uint64_t from_seq = 0,
                                               std::size_t limit = 100) const override;
  Result<bool> VerifyChain() const override;
  Result<std::size_t> RecoverDangling() override;

  // 테스트 제어: 특정 커밋 인덱스(1-indexed)에서 오류 주입
  void InjectFailureAt(std::size_t commit_index, Error error) noexcept;
  void ClearFailureInjection() noexcept;

#if defined(COGITO_TESTING)
  // 테스트 전용 변조 Seam (VerifyChain 위변조 탐지 검증용)
  void TamperEventPayload(std::size_t index, const ccj::Json& tampered_payload);
  void TamperEventHash(std::size_t index, const Digest& tampered_hash);
  void DeleteEventAt(std::size_t index);
#endif

  // 기록된 이벤트 전체 스냅샷 반환
  std::vector<AuditEvent> GetEvents() const;
  std::size_t event_count() const noexcept;

 private:
  std::vector<AuditEvent> events_;
  Digest                  head_hash_{};
  std::uint64_t           next_seq_ = 1;
  std::size_t             failure_index_ = 0;
  Error                   injected_error_{Error::Ok()};
};

}  // namespace cogito

#endif  // COGITO_AUDIT_HPP