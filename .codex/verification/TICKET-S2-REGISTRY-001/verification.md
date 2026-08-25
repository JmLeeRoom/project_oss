# TICKET-S2-REGISTRY-001 Verification Evidence

- Recorded: 2026-08-25T16:19:03+09:00
- Branch: `codex/ticket-s2-registry-001-implementation`
- Base HEAD: `8ccca61aef4fe7e6cc668177295b17ae645a6dee`
- Contract preflight: `PASS`
- Stage Exit Gate: `NOT_EVALUATED` (this is a ticket-only verification)

## Implemented ticket scope

- `src/tool.cpp`: stable enum strings, invalid-enum fallbacks, identifier/range checks, the complete Effect/Risk/Approval/Idempotency matrix, and Enabled/Forbidden lifecycle invariants.
- `src/registry.cpp`: atomic registration, provider binding, tri-state lookup, fully staged Freeze, schema accessors, exact digest projection, argument validation, mode-filtered model export, and move support.
- `tests/tool_test.cpp`: 6 ticket test cases, including all 72 matrix rows and all declared identifier/range/status boundaries.
- `tests/registry_test.cpp`: 15 ticket test cases, including rollback/state invariants, provider boundaries, schema coverage/skip behavior, the canonical two-tool digest, error mapping, zero handler calls on validation failures, export filtering/order, and moved-registry behavior.
- `src/CMakeLists.txt` and `tests/CMakeLists.txt`: production and test target integration.

No `include/**`, `docs/**`, or `config/**` contract file was changed by Codex for this ticket. Existing Gemini/user working-tree changes were preserved.

## Toolchains and dependencies

- Windows GCC: `g++.exe (Rev5, Built by MSYS2 project) 16.1.0`
- Linux GCC: `gcc (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0`
- Linux Clang: `Ubuntu clang version 14.0.0-1ubuntu1.1`
- CMake: `4.4.2`
- Ninja: `1.13.2`
- vcpkg tool: `2026-07-27-98d7cb0cf1f4686a3e43aa5672b6230c1d56bce8`
- vcpkg baseline: `127402f1c75bb3d5ff6bce04b285faa4930a5aca`
- Resolved packages: Catch2 `3.15.3`, nlohmann-json `3.12.0#2`, json-schema-validator `2.4.0`, OpenSSL `3.6.3`.

Official CMake, Ninja, vcpkg-tool, and dependency archives used to provision the external test environment were accepted only after their repository/vcpkg SHA-512 checks passed.

## Commands and results

### Windows GCC 16.1 Debug

```text
cmake --build C:\CogitoBuild\project_oss\registry-gcc-debug --parallel
ctest --test-dir C:\CogitoBuild\project_oss\registry-gcc-debug --output-on-failure
C:\CogitoBuild\project_oss\registry-gcc-debug\tests\cogito_tests.exe "[tool],[registry]"
```

- Build: `PASS` (`ninja: no work to do` on final confirmation; the prior build completed 18/18 steps).
- Full CTest: `63/63 PASS`, 4.51 seconds.
- Ticket tests: `21/21 PASS`, 362 assertions.
- Compiler policy: C++17 with `-Wall -Wextra -Wpedantic -Werror` from the project build.

### Windows GCC 16.1 Release

```text
cmake --build C:\CogitoBuild\project_oss\registry-gcc-release --parallel
ctest --test-dir C:\CogitoBuild\project_oss\registry-gcc-release --output-on-failure
```

- Build: `PASS` (`ninja: no work to do` on final confirmation; the prior build completed 18/18 steps).
- Full CTest: `63/63 PASS`, 3.16 seconds.

### Linux Clang 14 Debug

```text
cmake -S <repo> -B /home/yoonsy/cogito-build/registry-clang14-debug-direct -G Ninja \
  -DCMAKE_PREFIX_PATH=/home/yoonsy/cogito-build/registry-clang-asan-ubsan/vcpkg_installed/x64-linux \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++
cmake --build /home/yoonsy/cogito-build/registry-clang14-debug-direct --parallel
ctest --test-dir /home/yoonsy/cogito-build/registry-clang14-debug-direct \
  --output-on-failure --parallel 2
```

- Configure/build: `PASS`, 18/18 build steps.
- Full CTest: `63/63 PASS`, 1.07 seconds.
- Compiler policy: C++17 with `-Wall -Wextra -Wpedantic -Werror`.

### Linux GCC 11 ASan + UBSan

```text
cmake -S <repo> -B /home/yoonsy/cogito-build/registry-gcc11-asan-ubsan -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/home/yoonsy/.local/cogito-toolchains/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=g++ -DCOGITO_ENABLE_ASAN=ON -DCOGITO_ENABLE_UBSAN=ON
cmake --build /home/yoonsy/cogito-build/registry-gcc11-asan-ubsan --parallel
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  /home/yoonsy/cogito-build/registry-gcc11-asan-ubsan/tests/cogito_tests "[tool],[registry]"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  ctest --test-dir /home/yoonsy/cogito-build/registry-gcc11-asan-ubsan \
  --output-on-failure --parallel 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  /home/yoonsy/cogito-build/registry-gcc11-asan-ubsan/tests/cogito_tests --reporter compact
```

- Build: `PASS`, 18/18 steps.
- Ticket tests: `21/21 PASS`, 362 assertions.
- Full CTest: `63/63 PASS`, 3.12 seconds.
- Full assertion run: `63/63 PASS`, 801,262 assertions.
- ASan findings: `0`.
- LeakSanitizer findings: `0`.
- UBSan findings: `0`.

The first full Linux run lacked the German and Korean OS locales and therefore failed only the pre-existing locale-availability assertion. After generating `de_DE.UTF-8` and `ko_KR.UTF-8`, the unchanged binary passed 63/63. A separate Clang 14 ASan diagnostic binary exhibited an intermittent pre-main WSL runtime startup fault; the stable GCC ASan/UBSan lane above is the authoritative sanitizer evidence, while the non-sanitized Clang lane is the authoritative Clang compile/test evidence.

## Contract and safety evidence

- Canonical schema digests matched:
  - `calc.add`: `e1f97f4c393c55989d60303772331153d3aaced8fa68b2ef6b55ba4a1b7728ca`
  - `fs.delete`: `715b419cbee316e7ad17c7f8bfb0bc7059e3ac6ef5c0163856a9dce98ec1596d`
- Canonical registry digest matched:
  - `31c8148f2e21c391ec41e255d1d6a73749eb7ed1e28dd1264b712a6414bc2612`
- Duplicate, invalid-contract, provider-batch, schema-compile, digest-computation, and post-Freeze rejection tests prove state unchanged.
- `ValidateArguments` negative paths assert that the tool handler call count remains zero.
- Frozen `RegisterFrom` asserts that the provider is not invoked.
- Sensitive-pattern scan over all six ticket files: `0` hits.
- `git diff --check`: `PASS` (exit code 0).

## Workspace hygiene

- Generated workspace-local `out/` artifacts were moved, not deleted, to the recoverable external path `C:\CogitoBuild\project_oss\workspace-out-ticket-s2-registry-001`.
- All active build directories are outside the repository.
- No commit, push, tag, merge, signing, or PR action was performed.

