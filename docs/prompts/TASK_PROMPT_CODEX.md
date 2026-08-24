# [Gemini Task] Codex S0 빌드 시스템 구축 및 S1 CCJ v1 / LP Digest 코어 구현

> **작성자**: Gemini (설계·계약 관리)
> **수행자**: Codex (구현·빌드·테스트 전담)
> **기준일**: 2026-08-24
> **승인 상태**: **1단계 승인 완료 (G0-05, G0-09, G0-23, G0-26 및 ADR-0004 Accepted — S0/S1 제한 착수 허가)**

---

## 1. Objective (작업 목표)

### 1-1. S0: C++17 빌드 시스템 및 의존성 환경 구축
- **CMake 3.24+ 기반 빌드 스크립트 작성**:
  - 루트 `CMakeLists.txt` 및 `CMakePresets.json` (MSVC / Clang / GCC 및 Ninja 지원, C++17 표준 강제).
  - 타깃 구조:
    - `cogito_core` (**STATIC** 라이브러리, `include/`가 `PUBLIC` 헤더 디렉터리로 지정).
    - `cogito_tests` (테스트 실행 바이너리, CTest 연동).
- **`vcpkg.json` 및 `vcpkg-configuration.json` 매니페스트 작성**:
  - **vcpkg baseline**: `127402f1c75bb3d5ff6bce04b285faa4930a5aca` 고정
  - 필수 의존성: `nlohmann-json` (3.12.0), `openssl` (3.6.3), `catch2` (v3.15.3)

### 1-2. S1: CCJ v1 정규 직렬화 및 LP 다이제스트 코어 구현
- **`src/canonical_json.cpp`**:
  - `ccj::ParseStrict(text, limits)`: 중복 키(`Errc::DuplicateKey`), 비UTF-8(`Errc::NotUtf8`), 깊이 초과(`Errc::DepthExceeded`), 크기 초과(`Errc::TooLarge`) 거부.
  - `ccj::Serialize(json)`: CCJ v1 정규 직렬화기.
    - **키 정렬**: **UTF-16 code unit 오름차순** (RFC 8785 §3.2.3 준수).
    - **유니코드**: 유효한 UTF-8 원본 바이트열을 그대로 보존 (NFC 강제 정규화 금지, non-NFC 거부 금지).
    - **숫자**: C99 `%g` 최소 2자리 지수 표기 (`1e-07`, `-1e-07`, 선행 0 미제거). 64비트 정수 범위(`2^53+2` 등)는 정확한 십진수(`9007199254740994`)로 출력.
    - **실패**: 비UTF-8(`Errc::NotUtf8`), NaN/Infinity(`Errc::InvalidArgument`), 깊이 초과(`Errc::DepthExceeded`), 16MB 초과(`Errc::TooLarge`).
  - `ccj::SelfTest()`: 내장된 24개 골든 벡터 자가검증 (하나라도 불일치 시 기동 실패).
- **`src/ids.cpp`**:
  - `Digest::is_zero()`, `Digest::hex()` (소문자 64자), `Digest::FromHex()` (대문자·비hex·길이 불일치 엄격 거부).
  - `NewUuidV4()`, `ProcessEpochId()`, `InitProcessEpoch()` (OpenSSL 난수원 기반 기본 구현).
- **`src/digest.cpp`**:
  - `Sha256()`: OpenSSL EVP API 기반 암호학적 SHA-256 (`Result<Digest>` 반환, 실패 시 `Errc::Internal`).
  - `LpBuffer`: `LpBuffer::Create(domain_tag)` 팩토리 패턴 및 sticky error 상태 구현.
  - **9대 도메인 프로젝션 함수 9종 구현** (`Result<Digest>` 반환, fail-closed):
    - `ComputeActionDigest`, `ComputeOperationDigest`, `ComputePermitDigest`, `ComputeAuditDigest`, `ComputeToolSchemaDigest`, `ComputeRegistryDigest`, `ComputePolicyDigest`, `ComputeConfigDigest`, `ComputeModelDigest`.

---

## 2. Contract & Types (계약 및 헤더 사양)

Codex는 `include/cogito/**`에 선언된 공개 헤더 계약을 100% 준수해야 합니다.

### 2-1. 헤더 참조 목록 (Gemini 소유, Codex 읽기 전용)
- `include/cogito/canonical_json.hpp`: CCJ 직렬화, ParseStrict, ParseLimits, SelfTest
- `include/cogito/ids.hpp`: Digest, SessionId, ActionId, TurnId
- `include/cogito/digest.hpp`: 도메인 태그 9종, LpBuffer, ToolProjectionDto, PolicyRuleProjectionDto, 프로젝션 함수 9종
- `include/cogito/result.hpp`: Result<T>, Error, Errc, reason_code

### 2-2. LP(Length-Prefixed) 인코딩 바이트 규약 (ADR-0004 D5 확정)
```text
LP 바이트 레이아웃:
1. 도메인 태그: u32le(len) ‖ UTF-8 tag bytes (첫 번째 필드 필수)
2. 문자열 / 바이트열 / CCJ: u32le(len) ‖ raw bytes
3. U64 정수: raw u64le 8바이트 (별도 길이 접두사 없음, 십진 문자열 절대 금지)
4. Digest: raw 32바이트 (별도 길이 접두사 없음, hex 문자열 절대 금지)
5. 누락 / Optional 필드: 빈 문자열 "" (u32le(0)) 로 고정
```

---

## 3. 🧪 9개 도메인 프로젝션 표준 검증 벡터 (Golden Test Vectors)

Codex는 `tests/digest_test.cpp` 작성 시 아래의 9개 공식 벡터를 단언(assertion)하여 인코딩과 SHA-256 일치성을 검증해야 합니다:

### ① Action Digest
- **입력**: `domain="cogito-action-v1"`, `session_id="sess-001"`, `turn_id=1`, `action_id="act-001"`, `tool_name="test.tool"`, `arguments={"arg":1}`
- **기대 SHA-256 Hex**: `bc22fa11f50ecbf4ae8befc96ef912732d4e23a2531506c428f5e0d0c3b075c8`

### ② Operation Digest
- **입력**: `domain="cogito-operation-v1"`, `tool_name="test.tool"`, `arguments={"arg":1}`
- **기대 SHA-256 Hex**: `a087d03801d9e9f4533d9e2375283dda8d7762de5a6d767df85d671acae25ab1`

### ③ Permit Digest
- **입력**: `domain="cogito-permit-v1"`, `action_digest=[0x01]*32`, `subject_id="user-1"`, `mode=2`, `policy_digest=[0x02]*32`, `registry_digest=[0x03]*32`
- **기대 SHA-256 Hex**: `11d7e38abac240d1bbf13ef7b7ff662ccc57ebd231923069179f53985324c739`

### ④ Audit Digest
- **입력**: `domain="cogito-audit-v1"`, `prev_hash=[0x00]*32`, `event_id="evt-001"`, `session_id="sess-001"`, `turn_id=1`, `action_id="act-001"`, `wall_time="2026-08-24T00:00:00Z"`, `monotonic_ns=1000000`, `process_epoch_id="epoch-001"`, `kind="tool_result"`, `actor_type=1`, `actor_id="agent"`, `payload={"status":"ok"}`, `schema_version=1`
- **기대 SHA-256 Hex**: `a20a0bc1a0fe979ad6d988cc92af9a91294a80b2efb2f289180d472b6e065085`

### ⑤ ToolSchema Digest
- **입력**: `domain="cogito-toolschema-v1"`, `name="test.tool"`, `input_schema={"type":"object"}`, `output_schema=null`
- **기대 SHA-256 Hex**: `9bef4d123b8c74a87f0cf1d91bb8f1186545854f91ba10777a6b6f38db295e64`

### ⑥ Registry Digest
- **입력**: `domain="cogito-registry-v1"`, 1개 도구: `name="a.tool"`, `status="enabled"`, `forbidden_reason=""`, `toolschema_digest=[0x04]*32`, `grammar_coverage="full"`, `effect="none"`, `risk="low"`, `idempotency="safe"`, `approval_required=0`, `timeout_ms=3000`, `max_output_bytes=65536`, `provider_id="p1"`, `invoker_id="i1"`
- **기대 SHA-256 Hex**: `9203f42851aeea36e00bbb0a824032b4ae8c8692b4efa08c9192523239eeb786`

### ⑦ Policy Digest
- **입력**: `domain="cogito-policy-v1"`, `schema_version=1`, `default_decision="deny"`, 1개 규칙: `priority=100`, `rule_id="r1"`, `normalized_rule={"action":"allow"}`
- **기대 SHA-256 Hex**: `45e5f703f7e00563042b4f14e1adcb38fb8211f5faada74e00976b0a27c7d51a`

### ⑧ Config Digest
- **입력**: `domain="cogito-config-v1"`, `schema_version=1`, `normalized_config={"mode":"readonly"}`
- **기대 SHA-256 Hex**: `91daa0313f6836bd456339804217c1fbc2ea64c56157133ad84031c08b081737`

### ⑨ Model Digest
- **입력**: `domain="cogito-model-v1"`, `provider_id="prov-1"`, `model_id="mod-1"`, `weights_sha256="w-sha"`, `chat_template_digest="ct-d"`, `tokenizer_digest="tok-d"`, `quantization="q4_k_m"`
- **기대 SHA-256 Hex**: `a0cb8345b7fe36e8850a958968a2a602c3b733f229028eafab2292c877b98963`

---

## 4. Codex Write Scope (작업 파일 범위)

Codex는 오직 아래 명시된 구현 및 테스트 파일만 작성/수정합니다:

```text
[빌드 및 의존성]
- CMakeLists.txt
- CMakePresets.json
- vcpkg.json
- vcpkg-configuration.json

[구현 소스]
- src/CMakeLists.txt
- src/canonical_json.cpp
- src/digest.cpp
- src/ids.cpp

[테스트 소스]
- tests/CMakeLists.txt
- tests/canonical_json_test.cpp
- tests/digest_test.cpp
- tests/ids_test.cpp
```

---

## 5. Verification & Delivery (결과 보고 포맷)

작업 완료 후 다음 내용을 요약 보고해 주십시오:
1. 생성된 파일 목록
2. CMake 구성 및 컴파일 명령 (Exit code 0 확인)
3. Catch2 테스트 실행 결과 및 테스트 통과 건수
4. 발견된 이슈 또는 후속 단계 (S2 FSM 엔진) 제안
