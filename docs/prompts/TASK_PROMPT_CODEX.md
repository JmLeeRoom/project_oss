# [Gemini Task] Codex TICKET-S2-CONFIG-001 설정 로더, 비밀값 참조 해석 및 Config 다이제스트 구현 (체크리스트 S2-07, S2-08, S2-09 Config 영역)

> **작성자**: Gemini (설계·계약 총괄)
> **수행자**: Codex (구현·빌드·테스트 전담)
> **기준일**: 2026-08-25
> **티켓**: **TICKET-S2-CONFIG-001 (체크리스트 S2-07, S2-08, S2-09 Config 영역)**
> **승인 상태**: S0/S1 코어 및 S2 Schema/Registry 티켓은 제한 착수 승인 아래 구현·검증 완료, G0-26 Accepted, G0-LEDGER(:14, :114) S2 Config 승인 반영, ADR-0004 D6 Shape-only 투영 승인 반영 — S2 Config 구현 한정 착수 승인

---

## 1. Objective (단일 집중 티켓 목표)

S2 Schema(`tool_schema.cpp`), 기초 유틸리티(`secret_string.cpp`, `clock.cpp`), 및 Registry(`tool.cpp`, `registry.cpp`) 완료에 이어, S2의 마지막 컴포넌트인 **설정 로더(`ConfigLoader`), 엔진 설정(`CogitoConfig`, `EngineConfig`), 비밀값 참조 해석기(`SecretRef`, `ResolveSecret`), 권한 검사기(`CheckSecretFilePermissions`), 테스트 전용 Seam(`testing::SecretTestSeam`), 및 설정 다이제스트(`config_digest`)**를 구현하고 단위 테스트를 전수 통과시킵니다.

> **범위 한정 고지**: 본 티켓은 Config/Secret 구성요소의 구현 및 단위 검증을 전담하며, 전체 S2 Exit Gate 및 부팅 파이프라인 통합 판정은 후속 통합 단계에서 수행합니다.

---

## 2. Header & Contract Specification (Gemini 소유 공개 헤더 참조)

Codex는 `include/cogito/**`에 선언된 공개 헤더 계약을 100% 준수합니다:

### 2-1. 헤더 계약 목록
- [`include/cogito/config.hpp`](file:///C:/Users/yoonsy/Desktop/%EC%97%85%EB%AC%B4_2026/1.%20%EC%97%B0%EA%B5%AC%EC%86%8C/0.%20%ED%94%84%EB%A1%9C%EC%A0%9D%ED%8A%B8/2026/codyssey/project_oss/include/cogito/config.hpp): `SecretRef`, `ResolveSecret`, `CheckSecretFilePermissions`, `EngineConfig`, `CogitoConfig`, `ConfigLoader`, `testing::SecretTestSeam` (테스트 빌드 가드 `#if defined(COGITO_TESTING)`)
- [`include/cogito/secret_string.hpp`](file:///C:/Users/yoonsy/Desktop/%EC%97%85%EB%AC%B4_2026/1.%20%EC%97%B0%EA%B5%AC%EC%86%8C/0.%20%ED%94%84%EB%A1%9C%EC%A0%9D%ED%8A%B8/2026/codyssey/project_oss/include/cogito/secret_string.hpp): `SecretString`
- [`include/cogito/digest.hpp`](file:///C:/Users/yoonsy/Desktop/%EC%97%85%EB%AC%B4_2026/1.%20%EC%97%B0%EA%B5%AC%EC%86%8C/0.%20%ED%94%84%EB%A1%9C%EC%A0%9D%ED%8A%B8/2026/codyssey/project_oss/include/cogito/digest.hpp): `ComputeConfigDigest`
- [`include/cogito/canonical_json.hpp`](file:///C:/Users/yoonsy/Desktop/%EC%97%85%EB%AC%B4_2026/1.%20%EC%97%B0%EA%B5%AC%EC%86%8C/0.%20%ED%94%84%EB%A1%9C%EC%A0%9D%ED%8A%B8/2026/codyssey/project_oss/include/cogito/canonical_json.hpp): `ccj::Json`, `ccj::ParseStrict`, `ccj::ParseLimits`
- [`include/cogito/result.hpp`](file:///C:/Users/yoonsy/Desktop/%EC%97%85%EB%AC%B4_2026/1.%20%EC%97%B0%EA%B5%AC%EC%86%8C/0.%20%ED%94%84%EB%A1%9C%EC%A0%9D%ED%8A%B8/2026/codyssey/project_oss/include/cogito/result.hpp): `Errc::ConfigError`, `Errc::SecretError`, `Errc::SchemaViolation`, `Errc::InvalidArgument`, `Errc::DuplicateKey`, `Errc::NotUtf8`, `Errc::TooLarge`, `Errc::DepthExceeded`, `Errc::Forbidden`
- [`config/cogito.schema.json`](file:///C:/Users/yoonsy/Desktop/%EC%97%85%EB%AC%B4_2026/1.%20%EC%97%B0%EA%B5%AC%EC%86%8C/0.%20%ED%94%84%EB%A1%9C%EC%A0%9D%ED%8A%B8/2026/codyssey/project_oss/config/cogito.schema.json): G0-10 준수 및 `propertyNames` 명시 Draft-07 설정 스키마 정의

---

## 3. Implementation Specification (세부 구현 규범)

### 3-1. 비밀값 참조 해석 및 보안 검사 (`src/secret.cpp` — `S2-08`)

1. **`SecretRef` URI 문법 및 사전 검증 우선순위**:
   - URI 전체 길이: $1 \le len \le 1024$ 바이트, NUL 바이트(`\0`) 포함 금지.
   - URI 정규식: `^(env|file|wincred|keyring):(.+)$`
   - `SecretRef::scheme()`: 첫 번째 `:` 앞의 스킴 문자열 (`"env"`, `"file"`, `"wincred"`, `"keyring"`). 콜론이 없으면 `""`.
   - `SecretRef::location()`: 첫 번째 `:` 뒤의 위치 식별자 문자열. 콜론이 없으면 `""`.
   - **사전 검증 원칙**: URI 정규식 및 길이 제약 위반 시 `SecretTestSeam` 조회보다 앞서 즉시 `Errc::SecretError`를 반환합니다 (테스트 Seam을 통한 비정규 URI 주입 우회 원천 차단).

2. **비밀값 해석기 (`ResolveSecret`)**:
   - fail-closed: 실패 시 `Errc::SecretError`, `Errc::Forbidden`, `Errc::TooLarge` 반환.
   - **`testing::SecretTestSeam` 조회 (테스트 빌드 `#if defined(COGITO_TESTING)` 전용)**:
     - 정규 URI 사전 검증 통과 후, `SecretTestSeam`에 해당 `ref.uri`로 등록된 모의 비밀값이 있으면 해당 값을 `SecretString`으로 래핑하여 즉시 반환 (`thread_local` 격리 저장소, `ClearMockSecrets()` 시 `OPENSSL_cleanse` 소거).
     - 운영 빌드(`COGITO_TESTING` 미정의)에서는 Seam 코드가 컴파일되지 않음.
   - **`env:NAME` 스킴**:
     - `NAME` 형식: `^[a-zA-Z_][a-zA-Z0-9_]*$`, 길이 $1 \le len \le 256$ 바이트 (NUL 불가). 위반 시 `Errc::SecretError`.
     - `std::getenv(NAME)` 조회: 미설정이거나 빈 문자열이면 `Errc::SecretError`.
     - 값 크기 상한: $\le 65,536\text{B}$. 초과 시 `Errc::TooLarge`.
     - 성공 시 `SecretString(value)`로 반환.
   - **`file:/abs/path` 스킴 (TOCTOU 방지 동일 핸들 열기·검증·읽기)**:
     - `location()`은 반드시 절대 경로여야 함 (POSIX: `/`로 시작, Windows: 드라이브 문자 `X:\` 또는 UNC `\\`로 시작, 길이 $1 \le len \le 1024$, NUL 불가). 상대 경로는 `Errc::SecretError`.
     - **POSIX 파일 열기 및 핸들 검증**:
       * `open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)`으로 열기.
       * 대상이 심볼릭 링크인 경우 `ELOOP` 발생 $\rightarrow$ `Errc::Forbidden` 반환.
       * 파일 없음(`ENOENT`) $\rightarrow$ `Errc::SecretError`, 권한 없음(`EACCES` / `EPERM`) $\rightarrow$ `Errc::Forbidden`.
       * 동일 열린 fd에 대해 `fstat(fd, &st)` 검증:
         - 일반 파일(`S_ISREG(st.st_mode)`) 필수 (FIFO, 디렉터리, 소켓, 디바이스 발견 시 즉시 닫고 `Errc::Forbidden`).
         - 소유자: `st.st_uid == getuid() || st.st_uid == 0` 필수. 불일치 시 `Errc::Forbidden`.
         - 권한 모드: 오직 `0600` 또는 `0400`만 허용 (`(st.st_mode & 0777) == 0600 || (st.st_mode & 0777) == 0400`). 그룹/기타 권한 비트(`st.st_mode & 0077 != 0`) 또는 소유자 실행 비트(`st.st_mode & 0100 != 0`) 존재 시 `Errc::Forbidden`.
     - **Windows 파일 열기 및 핸들 검증 (DACL 화이트리스트 모델)**:
       * `CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL)`로 열기.
       * 열린 핸들에 대해 `GetFileInformationByHandle` 검사: `FILE_ATTRIBUTE_DIRECTORY` 또는 `FILE_ATTRIBUTE_REPARSE_POINT` 확인 시 즉시 닫고 `Errc::Forbidden`.
       * `GetSecurityInfo`로 Owner 및 DACL 화이트리스트 검사:
         - NULL DACL은 거부 (`Errc::Forbidden`).
         - Owner SID는 현재 프로세스 토큰 사용자 SID, Local System(`S-1-5-18`), Built-in Administrators(`S-1-5-32-544`) 중 하나여야 함.
         - DACL의 모든 `ACCESS_ALLOWED_ACE` 중 읽기 권한(`FILE_READ_DATA | GENERIC_READ`)을 부여받은 Trustee SID는 오직 [현재 사용자, SYSTEM, Administrators]만 허용. 그 외의 SID(Everyone `S-1-1-0`, Authenticated Users `S-1-5-11`, Users `S-1-5-32-545`, 기타 임의 도메인 그룹)에 읽기 권한이 부여된 경우 즉시 `Errc::Forbidden`.
     - **결정론적 읽기 및 개행 정규화**:
       * 원시 파일 크기: $1 \le size \le 65,536\text{B}$ (0바이트 빈 파일 $\rightarrow$ `Errc::SecretError`, $> 65,536\text{B}$ $\rightarrow$ `Errc::TooLarge`).
       * 바이트 버퍼 읽기 수행 (내부 바이너리 NUL `\0`은 온전히 보존).
       * **끝 개행 단일 제거 규약**: 버퍼 끝이 `\r\n`이면 정확히 1회의 `\r\n` 제거. 그렇지 않고 끝이 `\n`이면 정확히 1회의 `\n` 제거. 그 외 끝 바이트는 변경하지 않음.
       * 개행 제거 후 최종 비밀값 길이가 0바이트이면 `Errc::SecretError`.
   - **`wincred:TARGET` 스킴**:
     - TARGET 형식: UTF-8 문자열, $1 \le len \le 256$ 바이트, NUL 불가.
     - Windows 환경: `MultiByteToWideChar`로 UTF-16 변환 후 `CredReadW(target_w.c_str(), CRED_TYPE_GENERIC, 0, &pCred)` 호출.
       * `CredentialBlobSize` 범위: $1 \le size \le 65,536\text{B}$, 내용이 유효한 UTF-8이어야 함.
       * `SecretString`으로 복사 후 `OPENSSL_cleanse(pCred->CredentialBlob, pCred->CredentialBlobSize)` 소거 및 `CredFree(pCred)`.
     - 비Windows 환경 또는 자격증명 미발견 시 `Errc::SecretError` 반환.
   - **`keyring:SERVICE/USER` 스킴**:
     - Core v1 공식 규약: **v1에서는 미지원(Unsupported)**으로 확정 (`Errc::SecretError` 반환, `COGITO_TESTING` 모의 주입 시에만 통과).

3. **`CheckSecretFilePermissions(path)`**:
   - `file:` 경로에 대해 동일한 POSIX/Windows 보안 검사를 수행하고 유효하면 `Error::Ok()`, 위반 시 `Errc::Forbidden`을 반환합니다.

---

### 3-2. 설정 파싱, 정규화 직렬화 및 Config 다이제스트 (`src/config.cpp` — `S2-07`, `S2-09`)

1. **`CogitoConfig::Validate() const` (자체 유효성 사전 검증)**:
   - `schema_version != 1` $\rightarrow$ `Errc::SchemaViolation`
   - `engine`:
     - `max_concurrent_sessions`: $1 \le val \le 65536$
     - `default_turn_timeout_ms`: $100 \le val \le 3,600,000$
     - `log_level`: `"trace"`, `"debug"`, `"info"`, `"warn"`, `"error"` 중 하나
   - `secrets`:
     - 프로퍼티 키: $1 \le len \le 128$ 바이트, `^[a-zA-Z0-9_.-]+$` 준수.
     - 값: `SecretRef.uri`가 `^(env|file|wincred|keyring):.+$` 및 길이 $\le 1024$ 바이트 준수.
   - 위반 시 `Errc::SchemaViolation` 반환.

2. **`CogitoConfig::ToNormalizedJson()` (G0-26 / ADR-0004 D6 Shape-only 투영)**:
   - `secrets` 맵의 각 항목에 대해:
     - `env:NAME` $\rightarrow$ `"env:NAME"` 그대로 투영
     - `wincred:TARGET` $\rightarrow$ `"wincred:TARGET"` 그대로 투영
     - `keyring:SERVICE/USER` $\rightarrow$ `"keyring:SERVICE/USER"` 그대로 투영
     - `file:/abs/path` $\rightarrow$ `"file:<redacted>"`로 치환 투영 (ADR-0004 D6 승인: 호스트 절대 경로 차이로 인한 해시 불일치 방지).
   - 직렬화 결과 CCJ 객체 구조:
     ```json
     {
       "engine": {
         "default_turn_timeout_ms": 30000,
         "log_level": "info",
         "max_concurrent_sessions": 64
       },
       "secrets": {
         "api_key": "env:LLM_API_KEY",
         "db_pass": "file:<redacted>"
       }
     }
     ```

3. **`CogitoConfig::ComputeDigest()`**:
   - **`Validate()`를 먼저 호출**하여 유효하지 않은 설정 객체에 대해 즉시 `Errc::SchemaViolation`을 반환.
   - 검증 통과 시 `ComputeConfigDigest(schema_version, ToNormalizedJson())`를 호출하여 `Result<Digest>` 반환.

4. **`ConfigLoader` 파싱 및 스키마 검증**:
   - **Strict JSON 파싱 및 오류 보존**:
     - `FromJsonString(str)`은 `ccj::ParseStrict(str)`를 사용.
     - 잘못된 JSON 문법 $\rightarrow$ `Errc::InvalidArgument`.
     - 중복 키 $\rightarrow$ `Errc::DuplicateKey`.
     - 비UTF-8 바이트 $\rightarrow$ `Errc::NotUtf8`.
     - 중첩 깊이 초과 ($> 32$) $\rightarrow$ `Errc::DepthExceeded`.
     - 입력 크기 초과 ($> 256\text{ KiB} = 262,144\text{B}$) $\rightarrow$ `Errc::TooLarge`.
   - **프로그래밍 방식 구조 및 스키마 제약 검사 (`config/cogito.schema.json` 일치)**:
     - `schema_version`: 정수, 반드시 `1` (1 이외 값은 `Errc::SchemaViolation`).
     - `additionalProperties: false` 강제 (최상위 및 `engine` 내부 알 수 없는 필드 발견 시 `Errc::SchemaViolation`).
     - `engine`: 누락 시 기본값 (`max_concurrent_sessions: 64`, `default_turn_timeout_ms: 30000`, `log_level: "info"`). 범위 밖은 `Errc::SchemaViolation`.
     - `secrets`: 누락 시 빈 맵 `{}`. 키 이름 및 값 패턴 위반 시 `Errc::SchemaViolation`.
   - **`LoadFromFile(file_path)`**:
     - 파일 크기 상한: 256 KiB ($262,144\text{B}$). 초과 시 `Errc::TooLarge`.
     - 파일 열기 실패/미존재 시 `Errc::ConfigError`.
     - 읽은 문자열을 `FromJsonString()`으로 전달.

---

## 4. Canonical Test Fixtures & Golden Digest Vectors (통합 검증 벡터)

### 4-1. Golden Vector 1 (직접 `ComputeConfigDigest` 함수 호출 픽스처)
- `schema_version`: 1
- `normalized_config` CCJ: `{"mode":"readonly"}`
- `ComputeConfigDigest(1, ccj::ParseStrict(R"json({"mode":"readonly"})json").value())`
- **기대 SHA-256 hex**: `91daa0313f6836bd456339804217c1fbc2ea64c56157133ad84031c08b081737`

### 4-2. Golden Vector 2 (Canonical `CogitoConfig` 인스턴스의 `config.ComputeDigest()`)
- `schema_version`: 1
- `engine`: `max_concurrent_sessions = 64`, `default_turn_timeout_ms = 30000`, `log_level = "info"`
- `secrets`:
  - `"api_key"`: `"env:LLM_API_KEY"`
  - `"db_pass"`: `"file:/etc/secrets/db.pass"`
- **`ToNormalizedJson()` 직렬화 CCJ**:
  `{"engine":{"default_turn_timeout_ms":30000,"log_level":"info","max_concurrent_sessions":64},"secrets":{"api_key":"env:LLM_API_KEY","db_pass":"file:<redacted>"}}`
- **기대 SHA-256 hex**: `77d1657862fdf0b228c554009d115ec0e9494b7b355809c0a7e01e4cc9964bd5`

### 4-3. Golden Vector 3 (Default `CogitoConfig` 인스턴스의 `config.ComputeDigest()`)
- `schema_version`: 1
- `engine`: `max_concurrent_sessions = 64`, `default_turn_timeout_ms = 30000`, `log_level = "info"`
- `secrets`: `{}`
- **`ToNormalizedJson()` 직렬화 CCJ**:
  `{"engine":{"default_turn_timeout_ms":30000,"log_level":"info","max_concurrent_sessions":64},"secrets":{}}`
- **기대 SHA-256 hex**: `c78f2d4be2e13fec7057495c07eedb7ff3817e7a2a89ba7c12fa5038bd30fe64`

---

## 5. Codex Write Scope (작업 허용 6개 파일)

Codex는 오직 아래 6개 파일만 작성/수정합니다:

```text
[빌드 스크립트]
- src/CMakeLists.txt
- tests/CMakeLists.txt

[구현 소스]
- src/config.cpp
- src/secret.cpp

[단위 테스트]
- tests/config_test.cpp
- tests/secret_test.cpp
```

---

## 6. Verification Deliverables (검증 요구사항)

1. **빌드 성공**:
   - GCC 및 Clang 환경에서 경고 없이 빌드 완료 (`-Wall -Wextra -Wpedantic -Werror`).
   - 테스트 타깃에 `COGITO_TESTING=1` 정의 추가하여 `SecretTestSeam` 컴파일.
2. **단위 테스트 전수 통과 (`secret_test.cpp`, `config_test.cpp`)**:
   - `secret_test.cpp`:
     - `SecretRef::scheme()`, `location()` 파싱 검증.
     - `ResolveSecret`: URI 형식 위반 거부(`SecretError`), `env:NAME` 정상 해석 및 미설정 거부.
     - `ResolveSecret`: `file:/path` 절대 경로 정상 해석, 상대 경로 거부, 0B 빈 파일 거부(`SecretError`), 64KB 초과 거부(`TooLarge`).
     - 파일 개행 정규화: `\r\n` 및 `\n` 단일 제거, 개행만 있는 파일 거부, 바이너리 NUL 보존 검증.
     - `CheckSecretFilePermissions`: 0600/0400 통과, 그룹/기타 권한(0644, 0777) 거부(`Forbidden`), symlink/FIFO 거부.
     - `testing::SecretTestSeam`: URI 사전 검증 후 모의값 해석, `ClearMockSecrets()` 시 `OPENSSL_cleanse` 소거 검증.
     - `SecretString` 메모리 소거 및 Canary 유출 0건 확인.
   - `config_test.cpp`:
     - `ConfigLoader`: 유효 JSON 로드 및 기본값 보완 검증.
     - Strict JSON 오류 보존: 문법 오류(`InvalidArgument`), 중복 키(`DuplicateKey`), 비UTF-8(`NotUtf8`), 깊이 초과(`DepthExceeded`), 256KiB 초과(`TooLarge`) 확인.
     - 스키마 제약: `schema_version != 1` 거부, 알 수 없는 필드 거부, engine 수치 범위 밖 거부, `secrets` 빈 키 거부, 패턴 위반 거부(`SchemaViolation`).
     - `CogitoConfig::Validate()` 및 직접 생성 인스턴스 검증 우회 차단 확인.
     - `ToNormalizedJson()`: `file:` 참조의 `"file:<redacted>"` 치환 검증.
     - `ComputeConfigDigest` 및 `CogitoConfig::ComputeDigest()`: Golden Vector 1, 2, 3 일치 검증.
3. **메모리 및 정적 분석 무결성**:
   - ASan / UBSan 실행 시 메모리 누수 및 UB 0건.
4. **`git diff --check`**: 공백 오류 0건.
