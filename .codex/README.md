# Cogito++ Codex 운영 계층

이 디렉터리는 제품 구현이 아니라 Codex의 작업 방식만 고정한다. 현재 기본 상태는 `idle`이며 G0 Gate도 통과되지 않은 상태라 제품 파일 쓰기가 차단된다.

## 구성

- `config.toml`: GPT-5.6 Sol, Ultra, Fast tier, 최대 2개 읽기 전용 서브에이전트.
- `agents/default.toml`, `worker.toml`, `explorer.toml`: 일반 역할도 전부 읽기 전용으로 덮어써 루트만 쓰게 한다.
- `agents/contract-auditor.toml`: 구현 전 계약·선행조건·소유권 감사.
- `agents/exit-gate-verifier.toml`: 구현 후 원시 증거의 독립 검사와 해당 시점의 단계 Exit Gate 판정.
- `hooks.json`: 세션 문맥 복구, 쓰기 범위·Git 안전 장치, 증거 없는 종료 경고.
- `hooks/cogito_hooks.py`: 표준 라이브러리만 사용하는 단일 훅 구현.
- `rules/safety.rules`: 파괴적 Git 명령과 직접 배포 경로의 실행 정책.
- `stage-state.json` / `stage-state.schema.json`: 현재 한 개 티켓의 승인·허용 경로·검증 상태와 버전 3 계약. 기준 HEAD 이후의 커밋·작업트리·보호 경로의 ignored 파일까지 세션 델타로 검사한다.
- `.agents/skills/cogito-stage-owner`: 한 티켓을 구현과 테스트, 독립 검증까지 닫는 워크플로.

사용자 정의 서브에이전트는 전체 대화를 fork하지 않는다. `fork_turns = "none"` 또는 사용 중인 클라이언트의 동등 옵션으로 생성하고, 티켓·권위 절·허용 경로·명령·증거 위치를 위임 메시지에 직접 넣는다. 현재 CLI는 사용자 정의 agent type과 full-history fork를 함께 요청하면 거부한다.

## 최초 활성화

1. 동시 작업이면 Codex·Claude·Antigravity에 각각 별도 Git worktree와 별도 브랜치를 준다. 브랜치만 나누고 같은 worktree를 공유하지 않는다.
2. Codex를 Codex 전용 worktree의 저장소 루트에서 재시작한다.
3. 프로젝트 신뢰 상태를 유지한다. 신뢰되지 않은 프로젝트에서는 `.codex` 설정·훅·규칙이 로드되지 않는다.
4. CLI에서 `/hooks`를 열어 이 저장소의 훅 정의를 검토하고 신뢰한다.
5. `/agent`에서 `default`, `worker`, `explorer`, `contract_auditor`, `exit_gate_verifier`가 보이고 모두 read-only인지 확인한다.
6. `/skills`에서 `cogito-stage-owner`가 보이는지 확인한다.

현재 설치된 Codex 0.146.x에서 프로젝트 스킬 자동 탐색은 최신 문서와 과도기 차이가 있을 수 있다. 스킬이 보이지 않으면 Codex를 최신 버전으로 갱신한 뒤 다시 시작한다. 제품 작업보다 먼저 이 탐색을 확인한다.

## 티켓 시작

사용자가 G0와 계약 상태를 승인한 뒤 루트 Codex가 `stage-state.json`을 갱신한다. 먼저 `phase = "preflight"`로 사전감사 결과를 기록하고, `codex/<ticket>-<slug>` 브랜치로 전환된 것이 확인된 뒤에만 `phase = "implementation"`으로 바꾼다. 예시는 형식 설명일 뿐 승인으로 간주하지 않는다.

```json
{
  "active_ticket": "S1-05",
  "baseline_head_oid": "40- or 64-hex HEAD recorded before ticket work",
  "phase": "implementation",
  "g0_gate": "passed",
  "contract_review": "pass",
  "contract_reference": "approved ADR/header commit or digest",
  "allowed_write_paths": [
    "src/canonical_json.cpp",
    "tests/canonical/",
    "artifacts/verification/S1-05/"
  ],
  "shared_write": {
    "approved": true,
    "paths": ["artifacts/verification/S1-05/"],
    "reference": "exact user approval reference"
  },
  "verification": {
    "ticket": "S1-05",
    "commands": ["exact configure/build/test commands approved at preflight"],
    "evidence_dir": "artifacts/verification/S1-05/",
    "manifest_file": "artifacts/verification/S1-05/manifest.json",
    "verdict_file": "artifacts/verification/S1-05/verdict.json"
  }
}
```

`allowed_write_paths`에는 실제 티켓에 필요한 최소 경로만 넣는다. `passed`, `pass`, 경로, 검증 명령을 추정해서 채우면 안 된다. `artifacts/**` 같은 공유 경로는 정확한 경로와 사람 승인 참조가 함께 있어야 하며, Codex가 스스로 `approved = true`로 바꾸는 것은 승인이 아니다.

스크립트, 빌드, 테스트, 패키지 관리 명령은 `verification.commands`에 사전 기록한 문자열과 정확히 일치할 때만 훅이 허용한다. 임의 스크립트로 소유권 검사를 우회하지 못하게 하는 장치이므로, 명령을 실행하면서 사후 추가하지 말고 계약 감사 때 함께 확정한다. 그 밖의 셸 명령은 단일 read-only 검사 명령, 승인된 Git 동작, 또는 `cogito_hooks.py --validate-state|--fingerprint`만 허용하는 default-deny 방식이다.

## 완료 상태

실제 검증이 끝나기 전에는 `verification.verifier`를 `pass`로 바꾸지 않는다. `exit_gate_verifier`가 독립적으로 `PASS`를 반환하고 체크리스트 §18 증거가 저장된 뒤에만 다음과 같이 갱신한다.

```json
{
  "phase": "complete",
  "verification": {
    "ticket": "S1-05",
    "commands": ["actual commands run"],
    "evidence_dir": "artifacts/verification/<milestone>/<commit>/",
    "manifest_file": "artifacts/verification/<milestone>/<commit>/manifest.json",
    "verdict_file": "artifacts/verification/<milestone>/<commit>/verdict.json",
    "verifier": "pass",
    "stage_exit_gate": "not_evaluated",
    "head_oid": "recorded HEAD OID",
    "workspace_fingerprint": "sha256:recorded scope fingerprint"
  }
}
```

검증자가 `PASS`를 반환하고 마지막 제품 파일 변경이 끝난 뒤 저장소 루트에서 다음 명령을 실행한다.

```powershell
py -3 -B .codex\hooks\cogito_hooks.py --fingerprint
```

출력된 `head_oid`와 `workspace_fingerprint`를 `manifest.json`, `verdict.json`, `stage-state.json` 세 곳에 그대로 기록한다. `manifest.json`은 도구체인, 실행 명령, 종료코드, 비어 있지 않은 원시 출력, 수용 기준별 증거, redaction PASS를 포함한다. 또한 모든 command output과 acceptance evidence 경로를 키로 하고 해당 파일의 `sha256:<64-hex>`를 값으로 하는 정확한 `evidence_files` 매핑을 포함한다. `verdict.json`은 같은 티켓·HEAD·지문, 동일한 acceptance ID 목록, 그리고 정확한 manifest 바이트의 `manifest_sha256`을 포함한다. 이후 허용 범위·공유 승인·검증 계획·제품 또는 승인된 공유 파일·원시 증거 파일 내용이나 HEAD가 바뀌면 Stop 훅은 이전 증거를 오래된 것으로 간주하고 다시 검증하도록 차단한다.

`verifier = "pass"`는 선택한 티켓의 수용 기준을 독립 검증했다는 뜻이다. 단계의 마지막 티켓이 아니면 `stage_exit_gate`는 `not_evaluated`로 둔다. 한 티켓의 통과를 전체 S단계 Exit Gate 통과로 승격하지 않는다.

## MCP 선택

Codex 프로젝트 MCP는 읽기 전용 공식 `openaiDeveloperDocs` 하나만 둔다. 버전·API·Codex 설정 사실을 확인하는 용도이며 `required = false`라 네트워크 장애가 제품 작업 시작을 가로막지 않는다. GitHub PR·이슈·CI 메타데이터는 이미 연결된 GitHub 앱만 사용한다. 로컬 파일과 빌드 결과는 로컬 도구로 검증한다. 브라우저·Playwright는 Antigravity가 G0-33/S10 증거를 만들 때 사용하며, SQLite MCP를 통한 감사 DB 직접 접근은 사용하지 않는다.

이 개발용 문서 MCP는 Cogito++ 런타임 MCP 구현과 별개이며 G0-15 승인 전 제품 범위에 넣지 않는다. 파일시스템 MCP, SQLite MCP, 임의 `npx -y` 서버는 Codex 구성에 추가하지 않는다.

## 3자 인계

- Claude 인계: 충돌 절, 필요한 공개 계약 변경, 안전·호환성 영향, Codex가 실행할 acceptance test, 요구되는 승인 commit/digest.
- Antigravity 인계: 확정된 API/fixture, auth·CSRF·SSE 계약, CSP·offline·viewport 조건, 브라우저 버전·console·HAR·screenshot 증거.
- 사용자 인계: 승인해야 할 ADR/G0, 외부 공급망·설비·인증·서명 결정, 실제 실행 명령과 Exit Gate 판정.

훅은 실수를 줄이는 가드레일이며 완전한 보안 경계가 아니다. 셸 안에서 임의 스크립트가 수행하는 모든 쓰기를 정적으로 식별할 수 없으므로 AGENTS 규칙, 리뷰, Git diff, 독립 verifier를 함께 사용한다.

## 현재 통합 경고

Antigravity 소유의 `.agents/skills/mock-api-engine/SKILL.md`와 `.agents/skills/validate-ui-and-csp/SKILL.md`는 파일 앞에 UTF-8 BOM(`EF BB BF`)이 있다. Codex 0.146.0의 스킬 파서는 이를 frontmatter 앞의 문자로 해석해 `missing YAML frontmatter` 경고를 낸다. 내용은 수정하지 말고 Antigravity가 두 파일의 BOM만 제거한 뒤 `/skills`에서 다시 확인해야 한다. Codex 소유 `cogito-stage-owner` 스킬은 BOM 없이 검증됐다.
