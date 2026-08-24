# [Codex Task] Cogito++ S0 저장소 골격 및 CMake Preset 6종 구축 (S0-03 ~ S0-06)

> **수신자**: OpenAI Codex / GitHub Copilot CLI  
> **역할**: Cogito++ C++ 코어 구현 및 빌드 인프라 엔지니어  
> **소유 영역**: `src/**`, `tests/**`, `cmake/**`, `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`  
> **쓰기 금지**: `include/cogito/**`의 헤더 API 설계 임의 변경 금지 (Claude 소유)

---

## 1. 배경 및 미션

제품 코드(S1~) 착수 전, `Cogito++_구현명세서.md` §2(저장소 레이아웃) 및 §9(빌드 사양)에 맞춰 **C++17 크로스플랫폼 빌드 환경 및 Preset 6종**을 완벽하게 구축하십시오.

---

## 2. 세부 구현 단계

### Step 1: 디렉터리 레이아웃 생성
- `include/cogito/`, `src/`, `src/fakes/`, `src/abi/`, `tests/core/`, `tests/canonical/`, `cmake/`, `config/` 디렉터리 생성.

### Step 2: vcpkg 매니페스트 고정
- `vcpkg.json` 작성:
  - 의존성: `nlohmann-json` (3.11.3+), `nlohmann-json-schema-validator` (2.1.0+)
- `vcpkg-configuration.json` 작성 (레지스트리 및 baseline commit 고정).

### Step 3: 최상위 `CMakeLists.txt` 작성
- C++17 표준 강제 (`CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_STANDARD_REQUIRED ON`).
- 엄격한 경고 플래그 적용:
  - GCC/Clang: `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`
  - MSVC: `/W4 /WX /permissive- /w14242 /w14254`
- 빌드 옵션 설정:
  - `option(COGITO_BUILD_TESTS "Build tests" ON)`
  - `option(COGITO_BUILD_CLI "Build CLI" ON)`
  - `option(COGITO_BUILD_SHARED "Build shared library" OFF)`

### Step 4: `CMakePresets.json` (6종 Preset 완비)
1. `linux-debug` (Ninja + Clang/GCC, -O0 -g)
2. `linux-release` (Ninja + Clang/GCC, -O3 -DNDEBUG)
3. `linux-asan` (-fsanitize=address,undefined -fno-omit-frame-pointer)
4. `linux-tsan` (-fsanitize=thread)
5. `win-msvc-debug` (MSVC x64 Debug)
6. `win-msvc-release` (MSVC x64 Release)

### Step 5: Smoke Test 및 C ABI 헤더 스켈레톤 배치
- `include/cogito/result.hpp` 및 `include/cogito/cogito.h` 배치.
- `tests/core/smoke_test.cpp` 작성:
  - `Result<int>`, `Errc::Ok`, C ABI 버전 매크로 검증.
- `add_test(NAME core.smoke COMMAND smoke_test)` 등록.

---

## 3. 완료 기준 (Exit Gate)

- CMake 설정 및 빌드가 **컴파일 경고(Warning) 0건**으로 성공해야 함.
- `ctest --output-on-failure` 실행 결과 **100% tests passed, 0 tests failed**.
- **실제 실행한 터미널 명령어와 ctest 성공 로그를 출력하십시오.**
