// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 운영 로그 (감사와 완전히 분리된 계층)
//
// 규범 근거 : Cogito++_구현명세서.md §4-12, §7-5, §12-3
// G0 결정   : G0-25 (Accepted)
//
// ★ OpsLogger 는 AuditJournal 이 아니다. 절대 섞지 않는다.
//
//   AuditJournal : 손실 없는 순서 기록. 회전·드롭 없음. 커밋 실패는 fail-closed.
//                  법적·운영적 증거. 불변식 7·8 의 대상.
//   OpsLogger    : 디버깅용. 회전·드롭·레벨 필터 허용. 실패해도 실행을 막지 않는다.
//
//   운영 로그의 회전이나 드롭이 감사 기록에 영향을 주어서는 안 된다(요구사항 §4).
#ifndef COGITO_OPS_LOG_HPP
#define COGITO_OPS_LOG_HPP

#include <cstdint>
#include <string>

namespace cogito {

enum class LogLevel : std::uint8_t {
  Trace = 0, Debug, Info, Warn, Error, Critical, Off
};

const char* ToString(LogLevel) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
class OpsLogger {
 public:
  virtual ~OpsLogger() = default;

  // 구현은 스레드 안전해야 한다. AgentLoop 스레드 외에서도 호출된다
  // (HTTP 워커, 프로토콜 콜백 등).
  //
  // ★ 비밀값 유입 금지 계약 (§4-13, §7-5)
  //   호출자는 SecretString::Expose() 결과, config 의 `*_ref` 가 가리키는 값,
  //   API 키, 클라이언트 키, 인증서 개인키를 message/detail 에 넣지 않는다.
  //   구현은 추가로 마스킹을 시도할 수 있으나, 그것에 의존하지 않는다.
  //
  // ★ 외부 데이터 유입 주의 (불변식 10)
  //   LLM 출력·도구 결과·RAG 문서 원문을 그대로 로깅하지 않는다.
  //   크기 상한을 넘기면 절단하고 절단 사실을 표시한다.
  virtual void Log(LogLevel level,
                   const std::string& message,
                   const std::string& detail) = 0;

  virtual bool IsEnabled(LogLevel level) const noexcept = 0;

  // 편의 래퍼 — 레벨이 꺼져 있으면 문자열 조립 비용도 들이지 않는다.
  void Trace(const std::string& m, const std::string& d = {}) {
    if (IsEnabled(LogLevel::Trace)) Log(LogLevel::Trace, m, d);
  }
  void Debug(const std::string& m, const std::string& d = {}) {
    if (IsEnabled(LogLevel::Debug)) Log(LogLevel::Debug, m, d);
  }
  void Info(const std::string& m, const std::string& d = {}) {
    if (IsEnabled(LogLevel::Info)) Log(LogLevel::Info, m, d);
  }
  void Warn(const std::string& m, const std::string& d = {}) {
    if (IsEnabled(LogLevel::Warn)) Log(LogLevel::Warn, m, d);
  }
  // ⚠ 이름이 `Error` 가 아니라 `LogError` 인 이유 — 멤버 함수 `Error` 는 클래스 유효범위에서
  //   타입 `cogito::Error`(result.hpp)를 **가린다.** 그러면 OpsLogger 를 상속한 어떤 클래스도
  //   클래스 본문 안에서 `Error Flush();` 같은 선언을 쓸 수 없다:
  //     error: 'Error' does not name a type
  //   헤더 집합 자체는 컴파일되므로 구현을 시작하기 전에는 드러나지 않는다.
  //   `cogito::Error` 로 정규화하면 우회되지만, 그 규칙을 모든 파생 클래스가 기억해야 한다.
  void LogError(const std::string& m, const std::string& d = {}) {
    if (IsEnabled(LogLevel::Error)) Log(LogLevel::Error, m, d);
  }
  // Critical 은 레벨 필터를 거치지 않는다. 봉인 실패·프로세스 중단 직전에 쓴다.
  void Critical(const std::string& m, const std::string& d = {}) {
    Log(LogLevel::Critical, m, d);
  }
};

// 아무것도 하지 않는 구현. 테스트와 OpsLogger 미주입 경로의 기본값이다.
// nullptr 검사를 호출부마다 흩뿌리지 않기 위해 존재한다.
class NullOpsLogger final : public OpsLogger {
 public:
  void Log(LogLevel, const std::string&, const std::string&) override {}
  bool IsEnabled(LogLevel) const noexcept override { return false; }
};

// 테스트용 — 기록을 보관하고 조회한다.
// 비밀 canary 가 로그에 유입되지 않았는지 검사하는 데 쓴다(체크리스트 S2-08).
class RecordingOpsLogger;   // tests/support 에 정의

}  // namespace cogito

#endif  // COGITO_OPS_LOG_HPP
