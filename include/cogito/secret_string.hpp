// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 민감정보 보호 문자열 (SecretString)
//
// 규범 근거 : Cogito++_구현명세서.md §4-8, 체크리스트 S2-08(:443-450)
#ifndef COGITO_SECRET_STRING_HPP
#define COGITO_SECRET_STRING_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// SecretString — 메모리 zeroization 및 복사 금지 비밀값 래퍼
//
// [보안 불변식]
// - 복사 생성/대입 금지 (우발적 복사 방지). 이동(move)만 허용.
// - 소멸 시 버퍼를 즉시 0으로 덮어씀 (OPENSSL_cleanse 기반 zeroization).
// - Expose() 호출은 실제 사용 직전으로 최소화하며 로깅/감사 payload 에 직접 전달 금지.
// ─────────────────────────────────────────────────────────────────────────────
class SecretString {
 public:
  SecretString() = default;
  explicit SecretString(std::string secret);
  ~SecretString();

  SecretString(const SecretString&) = delete;
  SecretString& operator=(const SecretString&) = delete;

  SecretString(SecretString&& other) noexcept;
  SecretString& operator=(SecretString&& other) noexcept;

  bool empty() const noexcept { return data_.empty(); }
  std::size_t size() const noexcept { return data_.size(); }

  // 비밀값 원문에 접근 (로그나 감사에 절대 출력하지 말 것)
  std::string_view Expose() const noexcept { return data_; }

  void Clear();

 private:
  std::string data_;
};

// ─────────────────────────────────────────────────────────────────────────────
// testing::CleanseObserver — 수명 안전(Life-time safe) zeroization 검증 Seam
//
// [실행 모델 및 수명 규약]
// - 스레드 격리: thread_local 함수 객체로 저장되어 멀티스레드 테스트 환경에서 경합(data race) 없음.
// - 호출 시점: SecretString 메모리 소거(OPENSSL_cleanse) 직후, 메모리 해제 직전(유효 수명 내)에 호출.
// - 예외 정책: 콜백 내부에서 예외 발생 시 내부에서 swallow(catch(...))하여 소멸자의 noexcept 보장 및 자원 해제 무결성 유지.
// - 등록/해제: SetCleanseObserver()로 등록, ClearCleanseObserver()로 해제(nullptr 설정).
// ─────────────────────────────────────────────────────────────────────────────
namespace testing {
using CleanseObserver = std::function<void(const volatile void* ptr, std::size_t size)>;

void SetCleanseObserver(CleanseObserver observer);
void ClearCleanseObserver();
}  // namespace testing

}  // namespace cogito

#endif  // COGITO_SECRET_STRING_HPP
