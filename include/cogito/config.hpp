// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 설정 및 비밀값 참조 (CogitoConfig, EngineConfig, SecretRef)
//
// 규범 근거 : Cogito++_구현명세서.md §4-13(:1284-1324), §4-3(:463), 체크리스트 S2-07, S2-08, S2-09
// G0 결정   : G0-26 (Accepted — config_digest 도메인 태그 kConfig), ADR-0004 D6
#ifndef COGITO_CONFIG_HPP
#define COGITO_CONFIG_HPP

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "cogito/canonical_json.hpp"
#include "cogito/digest.hpp"
#include "cogito/result.hpp"
#include "cogito/secret_string.hpp"

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// SecretRef — 비밀값 참조 URI
//
// [보안 불변식]
// - 설정 파일/구조체에는 실제 비밀값이 존재하지 않으며 참조 URI만 담는다.
// - 지원 스킴:
//     * env:ENV_VAR_NAME          (환경 변수)
//     * file:/absolute/path       (절대 경로 파일 — POSIX 0600, Windows 적절한 ACL)
//     * wincred:target_name       (Windows Credential Manager)
//     * keyring:service/username  (OS Keyring)
// ─────────────────────────────────────────────────────────────────────────────
struct SecretRef {
  std::string uri;

  // URI 파싱 보조 함수
  std::string_view scheme() const noexcept;
  std::string_view location() const noexcept;
};

// 비밀값 해석 함수 (fail-closed)
// [검증 및 해석 순서]
// 1. URI 정규식 ^(env|file|wincred|keyring):.+$ 및 길이 <= 1024B 검증. 위반 시 즉시 Errc::SecretError.
// 2. testing::SecretTestSeam 에 등록된 모의 비밀값 확인 (테스트 환경 격리 주입).
// 3. 실 백엔드(env, file, wincred, keyring) 해석 수행. 실패 시 Errc::SecretError, Errc::Forbidden, Errc::TooLarge.
Result<SecretString> ResolveSecret(const SecretRef& ref);

// file: 참조 대상 파일의 접근 권한 검사 (POSIX 0600/0400, Windows 안전한 DACL)
Error CheckSecretFilePermissions(const std::string& path);

// ─────────────────────────────────────────────────────────────────────────────
// EngineConfig — 코어 엔진 실행 파라미터
// ─────────────────────────────────────────────────────────────────────────────
struct EngineConfig {
  std::uint32_t max_concurrent_sessions = 64;
  std::uint64_t default_turn_timeout_ms = 30000;
  std::string   log_level = "info";  // "trace", "debug", "info", "warn", "error"
};

// ─────────────────────────────────────────────────────────────────────────────
// CogitoConfig — 전체 엔진 설정 객체
// ─────────────────────────────────────────────────────────────────────────────
struct CogitoConfig {
  std::uint64_t schema_version = 1;
  EngineConfig  engine;
  std::map<std::string, SecretRef> secrets;  // std::map = 키 오름차순 보장

  // 자체 유효성 검증 (스키마 제약, 필드 범위, URI 정규식 검사)
  [[nodiscard]] Error Validate() const;

  // 정규화된 JSON 생성 (G0-26: 비밀값 원문 배제, file: 스킴의 머신 고유 절대 경로는 "file:<redacted>" 로 투영)
  ccj::Json ToNormalizedJson() const;

  // config_digest 계산 (G0-26, 도메인 태그 kConfig — 내부적으로 Validate() 선행 검증)
  Result<Digest> ComputeDigest() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// ConfigLoader — 설정 로드 및 Draft-07 스키마 검증기
// ─────────────────────────────────────────────────────────────────────────────
class ConfigLoader {
 public:
  // JSON 객체로부터 파싱 및 스키마 검증 (위반 시 Errc::SchemaViolation 또는 Errc::ConfigError)
  static Result<CogitoConfig> FromJson(const ccj::Json& json);

  // JSON 문자열로부터 파싱 및 스키마 검증
  static Result<CogitoConfig> FromJsonString(std::string_view json_str);

  // 파일 경로로부터 읽기, 크기 검사, 파싱 및 스키마 검증 (최대 256 KiB)
  static Result<CogitoConfig> LoadFromFile(const std::string& file_path);
};

// ─────────────────────────────────────────────────────────────────────────────
// SecretTestSeam — wincred/keyring 및 비밀값 모의 주입용 테스트 전용 Seam
// ─────────────────────────────────────────────────────────────────────────────
#if defined(COGITO_TESTING)
namespace testing {
struct SecretTestSeam {
  static void SetMockSecret(std::string_view uri, std::string_view secret_value);
  static void ClearMockSecrets();
};
}  // namespace testing
#endif

}  // namespace cogito

#endif  // COGITO_CONFIG_HPP
