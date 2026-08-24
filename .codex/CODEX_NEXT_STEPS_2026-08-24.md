# Cogito++ 현황 검증 및 Codex Next Steps

> 기준 시각: 2026-08-24 (Asia/Seoul)
>
> 기준 브랜치/HEAD: `codex/ops-bootstrap` / `699832169412b69079a385bff1e88645b40a6f83`
>
> 판정: **PRODUCT_WRITES_BLOCKED — G0 및 작업공간 격리가 끝나기 전 S0/S1 제품 파일을 수정하지 않는다.**

## 1. 결론

제시된 현황은 2026-08-21의 커밋 상태를 설명하는 자료로는 대체로 맞지만, 2026-08-24의 작업 트리 기준으로는 일부가 이미 오래됐고 S0 실행 계획에는 상위 계약과 충돌하는 내용이 있다.

핵심 결론은 다음과 같다.

1. G0는 통과하지 않았다. `G0-RESOLUTION-9.md`는 9건의 **Proposed 정정안**일 뿐이며, 24건을 닫지 못한다. ADR 0001과 0004도 `Proposed`이고 미결 항목이 남아 있다.
2. C++ 구현은 시작되지 않았다. 여섯 개의 공개 헤더 초안은 존재하지만 `src/`, CMake, preset, vcpkg manifest, smoke test는 모두 없다. 일부 헤더는 아직 존재하지 않는 다른 헤더를 include하므로 전체 공개 계약이 빌드 가능한 상태도 아니다.
3. Codex 훅 파일과 동적 ownership loader는 이미 복구됐다. 그러나 훅 테스트는 93개 중 24개가 실패하므로 HO-codex-001을 완료로 볼 수 없다.
4. HO-antigravity-001의 `/api/v1`, 인바운드 WebSocket, 비규격 FSM 문제는 커밋된 HEAD에는 남아 있지만 현재 미커밋 작업 트리에서는 상당 부분 수정됐다. 다만 필수 명령 ID, 인증/CSRF/SOD, SSE 재개, 전이표 권위, 승인 UI 의미론과 증거가 미완료다.
5. 현재 저장소에는 worktree가 하나뿐이고 그 안에 Antigravity 소유 변경과 Chrome 프로필 변동이 섞여 있다. 이는 `AGENTS.md:7`의 분리 worktree 규칙을 위반하므로 Codex 제품 작업보다 먼저 격리해야 한다.
6. `TASK_PROMPT_CODEX.md`는 현재 그대로 실행할 수 없다. 잘못된 vcpkg 포트명, 체크리스트와 다른 preset 이름, Claude/shared 소유 파일 포함, 여러 티켓 묶음, 축소된 Exit Gate 때문에 상위 문서와 충돌한다.

## 2. 제시된 주장별 검증

| 주장 | 판정 | 현재 근거 |
| --- | --- | --- |
| 3-Agent 소유권 구분 | **부분 확인** | 큰 틀은 맞다. 다만 Codex는 `tools/cli/**`, `tools/web_host/**`, `bindings/**`도 소유하고, `.agents/**` 중 `.agents/skills/cogito-stage-owner/**`는 Codex carveout이다. 미분류 경로는 `shared`다. `AGENTS.md:38-53`, `scripts/ownership-policy.json:19-44`. |
| G0 자기모순 9건 정정안 작성 | **확인, 미승인** | 정확히 9건을 다루지만 상태가 `Proposed — 사람 승인 대기`다. `docs/g0/G0-RESOLUTION-9.md:5-10`. |
| ADR 0001/0004 작성 | **확인, 미승인** | 두 파일 모두 존재하고 `Proposed`다. 0001은 TTL 및 실행 후 감사 실패 표현이 미결이고, 0004는 숫자 구현 선택 등 미결이 남아 있다. 각 ADR `:3`, 0001 `:158-164`, 0004 `:237-245`. |
| 핵심 C++ 헤더 6종 완료 | **부분 확인** | 여섯 파일은 존재한다. 그러나 `fsm.hpp`와 `invoker.hpp`는 아직 없는 `canonical_json.hpp`, `permit.hpp`, `tool.hpp`를 include한다. 따라서 “승인된 완성 계약”이나 “빌드 가능한 헤더 세트”로 부르면 안 된다. |
| Antigravity 산출물에 `/api/v1`, WebSocket, 비규격 FSM이 남음 | **HEAD에서는 참, 작업 트리에서는 오래된 주장** | HEAD에는 남아 있다. 현재 작업 트리는 `/api/v1` 0건, 15 route 조건, `EventSource('/api/events')`, WebSocket 400 거부, canonical 10-state 목록을 가진다. 모두 미커밋이며 독립 검증 전이다. |
| HO-antigravity-001은 32건 정정 티켓 | **문서 내부 불일치** | 제목과 심각도 합계는 32건이지만 표의 ID는 A1~F10 합계 41개다. Claude가 카운트/범위를 정정해야 한다. |
| 세 Antigravity `.agents` 파일에 구형 WS/경로가 남음 | **HEAD에서는 참, 작업 트리에서는 거짓** | `frontend-and-visual-qa.md`, `mock-api-engine/SKILL.md`, `validate-ui-and-csp/SKILL.md`의 현재 미커밋 버전은 SSE/15 endpoints/W6 rejection으로 바뀌고 BOM도 제거됐다. |
| `cogito_hooks.py` 복구와 동적 정책 로드가 필요 | **복구 자체는 완료, 품질 Gate는 실패** | 파일이 존재하고 `hooks.json`의 5개 이벤트가 참조한다. `scripts/ownership-policy.json`을 동적으로 읽고 longest-prefix/fail-closed 검사를 한다. 다만 전체 훅 테스트 24개가 실패한다. |
| Sanitizer 단계를 고쳐야 함 | **확인** | `AGENTS.md:72`와 `.agents/skills/cogito-stage-owner/SKILL.md:77`이 아직 `S4 through S6`으로 잘못 제한한다. 체크리스트는 S1, S6, S7, S9, S10, S11의 서로 다른 lane을 요구한다. 모든 단계에 ASan/UBSan/TSan 세 개가 동일하게 적용된다고 단순화해서도 안 된다. |
| S0-03~S0-06을 한 번에 구현하면 됨 | **거짓** | `AGENTS.md:26`은 정확히 한 티켓만 선택하도록 한다. 각 티켓을 별도 preflight/검증으로 처리해야 한다. |
| G0 승인과 S0 통과 뒤 S1/CCJ 착수 | **방향은 맞지만 조건이 더 강함** | G0 33건 전체 종료, S0-01~S0-07 및 S0 Exit Gate, 승인된 공개 계약이 먼저다. CCJ serializer는 S1-05, 3-컴파일러/locale/fuzz/sanitizer 증거는 S1-07에 해당한다. |

## 3. 현재 저장소 실측

### 3.1 계약 및 Gate

- `.codex/stage-state.json`은 `active_ticket: null`, `phase: idle`, `g0_gate: not_passed`, `contract_review: not_run`, 빈 write allowlist다.
- `py -3 -B .codex/hooks/cogito_hooks.py --validate-state`는 exit 0과 `{"valid":true,"schema_version":3}`을 반환했다.
- ADR 파일은 2개뿐이며 둘 다 Proposed, Accepted는 0개다. S0 Exit Gate는 ADR 0001~0008의 승인을 요구한다 (`Cogito++_개발_작업체크리스트.md:256-262`).
- `docs/traceability.md`와 `artifacts/` 증거 디렉터리는 없다.
- `G0-RESOLUTION-9.md:696-698`은 이 보고서가 닫지 못하는 G0 24건을 명시한다.

### 3.2 C++/빌드 구현

존재하지 않는 핵심 경로는 다음과 같다.

- `src/`, `src/fakes/`, `src/abi/`
- `tests/core/`, `tests/canonical/`
- `cmake/`, `config/`, `bindings/`
- `CMakeLists.txt`, `CMakePresets.json`
- `vcpkg.json`, `vcpkg-configuration.json`
- `include/cogito/cogito.h`
- `.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`, `.clang-tidy`
- `LICENSE`, `NOTICE`, `THIRD_PARTY_LICENSES.md`, `SECURITY.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`

따라서 configure/build/ctest 성공이나 S0 smoke 통과를 주장할 근거가 없다.

현재 PowerShell PATH 실측에서는 Git과 GCC 16.1만 확인됐고 CMake, Ninja, Clang, MSVC `cl`, vcpkg는 발견되지 않았다. 이는 설치 여부 전체를 단정하는 결과가 아니라 **현재 셸에서 S0 검증 명령을 실행할 준비가 안 됐다는 결과**다.

### 3.3 Codex 가드레일

HO-codex-001의 현재 상태는 다음처럼 보는 것이 정확하다.

| 항목 | 현재 상태 | 남은 일 |
| --- | --- | --- |
| C1 훅 파일 부재 | **부분 해소** | 파일/연결은 복구됐지만 훅 suite를 green으로 만들어야 한다. |
| C2 `stage-state` 자기 승인 | **미해소** | human-write-only 여부를 사람이 결정해야 한다. 결정 전 모든 gate 변경을 리뷰 대상으로 유지한다. |
| C3 정책 상수 복제 | **대체로 해소** | 동적 loader와 fail-closed test는 동작한다. handoff가 요구한 `utf-8-sig`가 아니라 `utf-8`을 사용하므로 BOM 정책도 정렬한다. |
| C4 stage-owner carveout 불일치 | **해소** | ownership policy에 명시적으로 반영됐다. |
| C5 sanitizer 범위 | **미해소** | `AGENTS.md`와 stage-owner skill 둘 다 정정한다. |
| C6 schema에 없는 필드 | **해소된 것으로 확인** | 현재 skill이 요구하는 stage/verification 필드는 schema v3에 존재하고 checked-in state validation이 통과한다. |

훅 전체 테스트 실측:

```text
command: py -3 -B -m unittest discover -s .codex/hooks/tests -p "test_*.py" -q
result:  Ran 93 tests
         FAILED (failures=24)
exit:    1
```

동적 ownership source와 missing-policy fail-closed 두 개의 표적 테스트는 2/2 통과했다. 전체 실패의 중심은 `.codex/hooks/cogito_hooks.py:1210-1219`가 모든 non-read-only Git을 먼저 일괄 거부해, `:1382` 이후의 사용자 승인 commit/push/integration 로직을 도달 불가능하게 만든 점이다. 현재 구현, 테스트, `AGENTS.md:93`, stage-state의 publication 필드가 서로 다른 정책을 말한다.

별도 발견으로, 공유 파일 `scripts/guard-scope.ps1`은 ownership policy를 읽지만 `case_sensitive:false`를 구현하지 않고 shared 경로를 무조건 허용한다 (`:16`, `:83-93`). 이는 Codex 소유 파일이 아니므로 임의 수정하지 말고 Antigravity/사람에게 정확한 공유 경로 승인과 함께 인계해야 한다.

### 3.4 Antigravity 산출물

현재 미커밋 변경에서 확인된 개선:

- `/api/v1` 제거 및 POST 7 + GET 8 route 골격
- dashboard의 `EventSource('/api/events')`
- inbound WebSocket upgrade 400 거부
- canonical 10-state 이름 사용
- 규범 CSP 방향과 BOM 제거
- `ws` package dependency 제거

그러나 다음 이유로 HO-antigravity-001 완료나 G0-33 PASS로 인정할 수 없다.

- `/api/turn`은 누락된 `command_id`를 서버가 생성하며, 모든 POST가 필수 client UUID를 요구하지 않는다.
- Origin을 substring으로 허용하고 거부하지 않으며, server-side CSRF/content-type/authentication/role/step-up/SOD 검사가 없다.
- SSE resume은 일부 audit replay만 있고 process epoch 변경 처리와 완전한 event 재생 계약이 없다.
- FSM explicit transition row는 18개이며, UI 그래프는 `/api/transitions`에서 생성되지 않고 HTML에 하드코딩돼 있다.
- UI는 pending approval 단일 슬롯/첫 항목 위주이고 verdict 전에 modal을 닫는다.
- expiry는 server monotonic time이 아니라 browser local interval에 의존한다.
- test 한 파일이 실제로 검증하지 않은 CSRF 등 여러 영역을 `VERIFIED`라고 출력한다.
- checklist §18 형식의 browser version, console/CSP, HAR, screenshot, 명령/exit code, redaction이 결합된 증거 packet과 독립 verdict가 없다.

이는 Antigravity 소유 작업이므로 Codex는 수정하지 않는다.

### 3.5 작업 트리와 writer 격리

- `git worktree list --porcelain`: worktree 1개.
- 최종 점검 전 실측 `git status --porcelain`: 70개 경로(53 modified, 8 deleted, 9 untracked). 브라우저 프로필 프로세스로 수치는 변동할 수 있다.
- Antigravity 소유 web/control 파일 10개가 변경돼 있고, 나머지 변경의 대부분은 `.chrome_test_profile/**`다.
- 저장소의 tracked file 999개 중 930개가 `.chrome_test_profile/**`다.
- 전체 `git diff --check`는 Antigravity 파일 EOF blank line과 Chrome 로그 trailing whitespace 때문에 exit 1이다.

이 상태에서 Codex 제품 작업을 시작하면 다른 writer의 변경이 Codex session delta와 검증 지문에 섞인다.

## 4. 현재 S0 지시서의 계약 충돌

`docs/prompts/TASK_PROMPT_CODEX.md`는 authority 순위상 마지막의 assignment context일 뿐이다 (`AGENTS.md:11-20`). 다음 항목을 고치기 전에는 실행 입력으로 사용하지 않는다.

| 항목 | 현재 prompt | 상위 계약 |
| --- | --- | --- |
| 티켓 범위 | S0-03~S0-06 묶음 | 정확히 한 티켓만 선택 (`AGENTS.md:26`). |
| vcpkg port | `nlohmann-json-schema-validator` 2.1.0+ | `json-schema-validator` 2.4.0 override (`Cogito++_구현명세서.md:2316-2348`, 요구사항 `:528-540`). |
| preset 이름 | `linux-debug`, `linux-asan`, `linux-tsan`, `win-msvc-*` 등 | 최소 `windows-msvc-debug`, `linux-gcc-debug`, `linux-clang-asan`, `linux-clang-tsan`, `linux-release`, `linux-arm64-release` (`체크리스트:193-203`). |
| shared library default | `COGITO_BUILD_SHARED OFF` | 구현명세 예시는 `ON` (`Cogito++_구현명세서.md:2380-2389`). 최종값을 계약으로 확정해야 한다. |
| public header | `result.hpp`, `cogito.h` 배치 | `include/**`는 Claude 소유이고 `cogito.h`도 아직 없다. Codex가 생성하면 소유권 위반이다. |
| `config/` | Codex가 디렉터리 생성 | `config/**`는 Claude 소유다. |
| S0-03 산출물 | 일부 디렉터리만 | 체크리스트는 root 정책/라이선스/보안 파일과 generated-source 정책까지 요구한다 (`:182-191`). 상당수가 shared/Claude 범위다. |
| S0-06 수용 기준 | warning 0 + ctest 100% | install/export, external consumer, fake/fuzz 비링크 증거까지 필요하다 (`:225-244`). |
| Stage Exit | prompt의 짧은 Exit Gate | 실제 S0 Exit Gate는 G0 zero, 모든 preset, offline reinstall, ADR 0001~0008 Accepted, install consumer를 요구한다 (`:256-262`). |

추가로 순환 Gate가 있다.

- S0 Exit Gate는 ADR 0001~0008이 Accepted여야 한다.
- ADR-0004 `:209`는 S1 전에 승인해야 한다고 한다.
- 같은 ADR `:233,239`는 S1의 3-컴파일러 결과 뒤에야 Accepted/구현 선택이 확정된다고 한다.

따라서 Claude/사람이 승인 전 compiler experiment를 별도 G0 검증으로 허용하거나, ADR 상태 전이와 S0 Exit 조건을 고쳐야 한다. Codex가 편의상 한 방향을 선택하면 안 된다.

## 5. Codex Next Steps — 실행 순서

### P0. 즉시 유지할 상태

1. S0/S1 제품 파일을 쓰지 않는다.
2. `.codex/stage-state.json`의 `g0_gate`를 통과로 바꾸지 않는다.
3. 현재 Antigravity/Chrome 변경을 reset, restore, delete, move하지 않는다.
4. S0용 baseline SHA, compiler patch, license, target 이름, API를 임의로 만들지 않는다.

### P1. 작업공간 격리 — 사람/Antigravity 협조 필요

1. 현재 Antigravity 소유 10개 변경을 보존한 채 Antigravity 전용 branch/worktree로 옮긴다.
2. Codex, Claude, Antigravity에 각각 별도 branch와 worktree를 준다.
3. `.chrome_test_profile/**`의 추적/보관/ignore 정책은 shared 경로 결정으로 승인받아 별도 정리한다. Codex가 삭제하지 않는다.
4. Codex worktree가 깨끗한 baseline인지 확인한 뒤에만 다음 제어 평면 작업을 시작한다.

### P2. HO-codex-001 제어 평면 종결

이 작업은 S0 제품 구현과 분리한다.

1. **사람 결정**: `stage-state.json`을 human-write-only로 둘지, 리뷰 가능한 Codex-control write로 둘지 확정한다.
2. **Git mutation 정책 정렬**: 권장안은 `AGENTS.md:93`대로 “명시적 사용자 요청 + 안전한 exact form”을 허용하고, 훅의 조기 blanket deny를 제거/재배치해 기존 세부 검사를 도달 가능하게 하는 것이다. blanket deny를 유지하려면 publication schema, README, AGENTS, 테스트를 모두 그 정책으로 고쳐야 한다.
3. ownership loader의 BOM 계약을 `utf-8-sig` 또는 “정책 파일은 BOM 금지” 중 하나로 통일하고 테스트한다.
4. sanitizer 문구를 단계별 실제 lane으로 정정한다.
   - S1: CCJ fuzz + ASan/UBSan
   - S6: Core/SQLite ASan/UBSan + concurrency/read-path TSan
   - S7: lifetime/leak 및 ABI 경계 ASan/LSan/Windows 도구 + thread 경로 TSan
   - S9: UA object 누수 검사
   - S10: native host/web sanitizer·concurrency lane과 browser 증거를 소유권에 맞게 분리
   - S11: 전체 적용 가능한 release assurance lane
5. `.agents/skills/cogito-stage-owner/references/source-map.md:54`의 오래된 Antigravity prompt 경고와, Antigravity 수정이 확정된 뒤 `.codex/README.md:111-113`의 BOM 경고를 갱신한다.
6. 다음 검증이 모두 통과해야 HO-codex를 닫는다.

```powershell
py -3 -B .codex\hooks\cogito_hooks.py --validate-state
py -3 -B -m unittest discover -s .codex\hooks\tests -p "test_*.py" -v
git diff --check -- AGENTS.md .codex .agents/skills/cogito-stage-owner
```

수용 기준은 state validation exit 0, 훅 테스트 **93/93**, diff check exit 0, missing/corrupt ownership policy fail-closed, 대소문자/longest-prefix/shared approval 테스트 통과다.

공유 `scripts/guard-scope.ps1` 결함은 정확한 shared 경로 승인을 받은 별도 Antigravity/공동 guard 티켓으로 넘긴다.

### P3. Claude/사람 계약 선행 작업

Codex가 다음 입력을 받기 전 S0를 시작하지 않는다.

1. G0-01~G0-33 각각의 owner, 결정일, 승인 근거 및 안전 차단 0건.
2. `G0-RESOLUTION-9`의 사람 승인 또는 반려/수정. 이 문서만 승인해서는 G0 Gate가 닫히지 않는다.
3. ADR 0001~0008의 존재와 필요한 Accepted 상태.
4. ADR-0004/S0/S1 순환 Gate의 공식 해소.
5. G0-11(vcpkg feature/baseline), G0-12(CMake install/export), G0-22(CI/toolchain/signing) 결정.
6. `TASK_PROMPT_CODEX.md`의 상위 계약 정렬.
7. S0-03과 S0-07의 Claude/shared/Codex 산출물 분할 및 각 경로 승인.
8. 승인된 공개 헤더 commit/digest. Codex가 `include/**`를 임의 보완하지 않는다.

### P4. S0를 정확히 한 티켓씩 실행

모든 티켓에서 `$cogito-stage-owner`를 사용하고, 쓰기 전에 `contract_auditor` PASS, exact allowlist/verification/evidence, `codex/<ticket>-<slug>` branch를 준비한다. 루트만 쓰고 subagent는 읽기 전용으로 둔다.

1. **S0-03 저장소 골격**
   - 먼저 Claude/shared/Codex 산출물을 분할한다.
   - Codex는 승인된 `src/**`, non-web `tests/**`, `cmake/**` subset만 쓴다.
   - `include/**`, `config/**`, `docs/**`는 Claude가 제공한다.
   - root shared 정책/라이선스 파일은 정확한 경로별 사람 승인 없이는 쓰지 않는다.
2. **S0-04 toolchain/preset**
   - checklist의 6개 최소 preset 이름과 서로 분리된 build directory를 사용한다.
   - warnings-as-errors는 project code에만 적용한다.
   - G0-22의 실제 compiler/CI matrix를 기록한다.
3. **S0-05 vcpkg 공급망**
   - 실제 40-hex baseline과 registry commit을 고정하고 placeholder를 금지한다.
   - `nlohmann-json 3.11.3`, `json-schema-validator 2.4.0` 및 각 feature를 그 baseline에서 dry-run한다.
   - air-gap/cache/license/SBOM seed 증거를 남긴다.
4. **S0-06 CMake/test harness**
   - configure/build/test뿐 아니라 install/export 및 source tree 밖 consumer smoke를 수행한다.
   - fakes/fuzz가 production target에 링크되지 않음을 증명한다.
   - Claude가 제공한 승인 헤더만 소비한다.
5. **S0-07 CI/traceability**
   - Codex 소유 구현과 shared/Claude CI·traceability 산출물을 분할하고 승인한다.
6. 각 티켓은 독립 `exit_gate_verifier` PASS를 받아야 한다. 마지막 필수 티켓 또는 명시적 stage-close에서만 별도 S0 Exit Gate를 평가한다.

### P5. S1 준비와 CCJ 판정

S0 Exit Gate PASS 뒤에도 S1 전체를 한 번에 시작하지 않는다.

1. S1-01~S1-04를 순서와 승인 계약에 따라 각각 닫는다.
2. S1-05에서 CCJ serializer와 positive/boundary/negative test를 하나의 root writer stream으로 구현한다.
3. 숫자 포맷 구현은 승인된 ADR-0004의 선택을 따른다. 전역 `setlocale` 의존은 금지한다.
4. `to_chars(general, P)`와 locale-pinned `snprintf` 후보 비교가 선행 실험으로 승인되면 MSVC/GCC/Clang의 동일 corpus 산출 바이트와 SHA-256을 보존한다.
5. S1-06 digest, S1-07 3-compiler/locale/fuzz/ASan-UBSan을 각각 닫는다.
6. CCJ serializer와 모든 digest projection은 동일 root implementation stream으로 유지한다.

## 6. 에이전트별 의존 Next Step

| Owner | 다음 책임 |
| --- | --- |
| 사람 | G0/ADR 승인, C2 stage-state 권한, ADR-0004 순환 Gate, G0-22 toolchain/CI, shared 경로, Chrome profile 정책 결정. |
| Claude | G0 24건 잔여 계약, ADR 0001~0008, 공개 헤더, S0 prompt/체크리스트 정렬, HO-antigravity 카운트 및 API/위협모델 계약 정정. |
| Antigravity | 현재 변경을 전용 worktree로 격리하고 남은 auth/CSRF/SOD/command-id/SSE/FSM/UI/test 증거를 완성. shared guard의 case/shared 승인 문제를 사람 승인 아래 정리. |
| Codex | HO-codex 제어 평면을 green으로 만든 뒤 대기. G0/S0 선행 Gate가 닫히면 정확히 한 S0 티켓씩 구현·테스트·독립 검증. |

## 7. 착수/완료 판정 체크포인트

Codex가 S0 제품 쓰기를 시작할 수 있는 최소 조건:

- [ ] 전용 clean worktree와 exact ticket branch
- [ ] `.codex/stage-state.json`에 exact ticket, baseline HEAD, G0 passed, contract audit PASS, 최소 allowlist, exact commands, evidence path
- [ ] G0-01~G0-33 종료 증거와 필요한 사람 승인
- [ ] 현재 티켓의 Claude/shared 산출물 및 ownership 분할 완료
- [ ] 현재 환경 또는 CI에서 필요한 toolchain 사용 가능
- [ ] `contract_auditor` PASS

한 티켓을 완료했다고 말할 수 있는 최소 조건:

- [ ] production behavior와 positive/boundary/negative test
- [ ] 적용 가능한 rejected path의 state unchanged/write 0
- [ ] exact command, exit code, toolchain, raw output, sanitizer, redaction 증거
- [ ] `git diff --check` 및 changed/untracked ownership 점검
- [ ] `exit_gate_verifier`의 독립 PASS
- [ ] stage 마지막 티켓이 아니면 `STAGE_EXIT_GATE: NOT_EVALUATED`

## 8. 최종 상태 문구

현재 Codex 상태는 다음과 같다.

```text
BLOCKED_BY_CONTRACT_AND_WORKTREE_ISOLATION

제품 구현: 시작 불가
Codex control-plane: 부분 복구, 전체 테스트 실패(93개 중 24 실패)
G0 Gate: NOT_PASSED
S0 Gate: NOT_EVALUATED
S1: NOT_STARTED
다음 owner: 사람/Claude/Antigravity 선행 조치 → Codex HO-codex 종결 → exact S0 ticket
```
