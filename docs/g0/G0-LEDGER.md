# G0 결정 추적 대장 (G0-01 ~ G0-33)

| | |
| --- | --- |
| **목적** | `Cogito++_개발_작업체크리스트.md:109` — "G0-01~G0-33 각각에 owner, 결정일, ADR/이슈 링크를 배정했다" 를 충족할 추적 수단 |
| **작성** | Claude (계약 관리자 · 감사관), Gemini (정합화) |
| **기준일** | 2026-08-25 |
| **원본 대장** | `Cogito++_개발_작업체크리스트.md:71-105` (§2-2 G0 결정 대장) |
| **상태** | 이 파일은 **추적표**이며 실제 결정 권위는 승인된 ADR 및 확정된 계약 문서에 있다 |

## 작성 16 / 승인 7 (G0-05, 10, 23, 25, 26, 27, 29) / 미착수 17   (합계 33)

> **사람 승인 완료:** G0-05, G0-10, G0-23, G0-25, G0-26, G0-27, G0-29 및 `ADR-0004`가 **Accepted** 되었습니다.
> 전체 G0 Exit Gate 6개 체크는 아직 미충족 상태이나, **S0, S1 및 S2(Action/ToolSchema/ToolContract/Registry/Config/Policy/Clock/SecretString) 코어 구현에 한해 단계별 제한적 착수가 허가**되었습니다.

---

## 1. 상태 값 정의

이 대장은 다음 5개 값만 쓴다.

| 상태 | 의미 |
| --- | --- |
| `Proposed` | 정정안·ADR 초안이 파일로 존재하나 **사람 승인 대기**. `Resolved` 아님 (현재 9건) |
| `Accepted` | 승인권자가 승인함. 현재 **7건** (G0-05, G0-10, G0-23, G0-25, G0-26, G0-27, G0-29) |
| `Rejected` | 승인권자가 기각함. 현재 0건 |
| `Out of Scope` | 릴리스 범위에서 명시적으로 제외 확정됨. 현재 0건 |
| `미착수` | 정정안·ADR 초안이 아직 없음 (현재 17건) |

**결정일 열**은 `Accepted` / `Rejected` 로 확정된 날짜만 적는다. 초안 작성일은 비고에 둔다.

---

## 2. G0 대장 (33행)

| G0 | 차단 등급 | 요지(1줄) | 상태 | 담당 | 산출물 / ADR | 결정일 | 비고 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| G0-01 | 전체 차단 | ABI v1.0/v1.1 혼재, 예외 래퍼는 이미 요청별 `Subject` 사용 | Proposed | 작성 Claude · 승인 아키텍트 | `docs/g0/G0-RESOLUTION-9.md` ① | — | 초안 2026-08-21. 되돌림 가능(동 문서 `:21`). 전용 ADR 미배정 |
| G0-02 | 전체 차단 | `AgentLoop` 고정 `Subject` vs ABI/Web 요청별 주체 전달 불일치 | 미착수 | 미배정 | ADR `0003-approval-and-identity` (미작성) | — | ADR 0003 예약: `.claude/skills/adr-draft/SKILL.md:15` |
| G0-03 | 안전 차단 | `Respond` 가 nonce·requester 를 받지 않아 자기 승인 분리 검사 불가 | 미착수 | 미배정 | ADR `0003-approval-and-identity` (미작성) | — | 불변식 5 직결(`CLAUDE.md §5`). 안전 차단 |
| G0-04 | 안전 차단 | indeterminate 후 강제 `Ask` 강등과 즉시 `Deny(indeterminate_lockdown)` 가 상충 | 미착수 | 미배정 | 미배정 | — | 예약 ADR 없음 — ADR 번호 배정 필요 |
| G0-05 | 안전 차단 | 잠금 키에 쓰는 `action_digest` 가 재요청마다 달라져 잠금이 성립하지 않음 | **Accepted** | 아키텍트 + 안전 책임자 | `docs/g0/G0-RESOLUTION-9.md` ② · `docs/adr/0004-audit-integrity-and-failure.md` | 2026-08-24 | **되돌림 불가**. `operation_digest` 신설 승인 완료 |
| G0-06 | 감사 차단 | `RetryFinalize()` 가 원래 `TurnOutcome` 대신 새 Failed outcome 으로 `turn_end` 생성 가능 | 미착수 | 미배정 | 미배정 | — | `SKILL.md:16` 은 ADR 0004 담당이라 하나 실제 0004 는 미포함 → §4 |
| G0-07 | 동시성 차단 | 실행 중 cancel 명령을 dequeue 할 수 없어 취소 경로가 막힘 | 미착수 | 미배정 | 미배정 | — | 불변식 11 직결. thread model ADR 번호 미배정 |
| G0-08 | ABI 차단 | `agent_destroy` 가 `void` 인데 wrong-thread 오류 반환 요구, `@thread` 분류 누락 | 미착수 | 미배정 | 미배정 | — | `Cogito++_개발_작업체크리스트.md:176` 이 ADR 기록을 요구하나 번호 미지정 |
| G0-09 | Core 차단 | 무값 성공용 `Result<void>` 와 예외 허용 범위가 없음 | Proposed | 작성 Claude · 승인 아키텍트 | `docs/g0/G0-RESOLUTION-9.md` ③ | — | 초안 2026-08-21. S0/S1/S2 한정 승인 |
| G0-10 | Schema 차단 | pattern 200ms 상한 필수인데 C++17 표준 정규식에 안전한 취소/timeout 없음 | **Accepted** | 아키텍트 | `docs/g0/G0-10-regex-timeout.md` | 2026-08-25 | **S2 착수 한정 승인**. 256B, 복잡도 제한 및 결정론적 스텝 예산 확정 |
| G0-11 | 빌드 차단 | vcpkg `web` feature 자기 의존·overrides/baseline 조합이 실제 baseline 에서 미검증 | 미착수 | 미배정 | ADR `0002-build-time-adapters` (미작성) | — | 저장소에 `vcpkg.json` 부재(2026-08-24 루트 실측) — 검증 자체가 불가 |
| G0-12 | 패키지 차단 | 설치 target 이름이 빌드 트리 alias `cogito::core` 와 달라질 수 있음 | 미착수 | 미배정 | ADR `0002-build-time-adapters` (미작성) | — | 저장소에 `CMakeLists.txt` 부재(2026-08-24 루트 실측) |
| G0-13 | Provider 차단 | llama.cpp 반입/링크 방식·commit SHA·tokenizer digest 대상이 없음 | 미착수 | 제품 책임자 (명세 미지정 — 배정 필요) | 미배정 | — | **AI 결정 불가** (`docs/g0/G0-RESOLUTION-9.md:697`). 공급망 결정은 사람 |
| G0-14 | OPC UA 차단 | CAS 조건·NodeId/Variant 타입·허용오차·read-back timeout·manifest 필드 없음 | 미착수 | 제품 책임자 (명세 미지정 — 배정 필요) | 미배정 | — | **AI 결정 불가** (`:697`) — 실설비값. 임의 기본값 금지 |
| G0-15 | 범위 차단 | §1 이 참조하는 MCP §10-3 이 실제로 없고 구현 파일·단계도 없음 | 미착수 | 미배정 | 미배정 | — | MVP 포함/제외 결정 시 `Out of Scope` 로 전이 가능 |
| G0-16 | 범위 차단 | MQTT/RAG 는 빌드 옵션만 있고 계약·테스트 없음, C# 은 폴더·시연 요구만 | 미착수 | 제품 책임자 (명세 미지정 — 배정 필요) | 미배정 | — | **AI 결정 불가** (`docs/g0/G0-RESOLUTION-9.md:698`) — 릴리스 범위 |
| G0-17 | Web 차단 | 인증원·step-up·승인 역할·SOD 예외·OT-IT 범위·브라우저·감사 필드가 §12-14 미결 | Proposed | **제품 책임자** | `docs/g0/G0-17-QUESTIONNAIRE.md` | — | **AI 결정 불가** (`:697`). 산출물은 **질문지이며 결정이 아니다** — 이 파일만으로 G0-17 은 닫히지 않는다. 승인자 지정 근거: `Cogito++_개발_작업체크리스트.md:89`, `Cogito++_구현명세서.md:3025` |
| G0-18 | Web 차단 | 로그인/로그아웃·세션 쿠키·CSRF 발급·회전·OIDC bootstrap 경로 없음 | Proposed | 작성 Claude · 승인 미배정 | `docs/g0/G0-18-20-web-contract.md` | — | 초안 2026-08-24(병렬 작성). ADR 0009 예약(`SKILL.md:21`) |
| G0-19 | Web 차단 | body schema·오류 envelope/status mapping·command cache TTL·보존 상한 없음 | Proposed | 작성 Claude · 승인 미배정 | `docs/g0/G0-18-20-web-contract.md` | — | 초안 2026-08-24(병렬 작성). ADR 0009 예약 |
| G0-20 | Web 차단 | `assets_digest` 를 Web Host 가 Core 로 전달할 ABI/config 진입점 없음 | Proposed | 작성 Claude · 승인 미배정 | `docs/g0/G0-18-20-web-contract.md` | — | 초안 2026-08-24(병렬 작성). ADR 0009 예약 |
| G0-21 | 운영 차단 | SQLite migration/versioning·WAL/디스크 임계값·보존/백업/복구 정책 없음 | Proposed | 작성 Claude · 승인 미배정 | `docs/g0/G0-21-storage-migration.md` | — | 초안 2026-08-24(병렬 작성). `SKILL.md:16` 은 ADR 0004 담당이라 하나 실제 0004 는 미포함 → §4 |
| G0-22 | 품질 차단 | CI 공급자·최소 compiler patch·ARM64 방식·release signing 주체 없음 | 미착수 | 제품 책임자 (명세 미지정 — 배정 필요) | ADR `0008-packaging-and-airgap` (미작성) | — | **AI 결정 불가** (`docs/g0/G0-RESOLUTION-9.md:698`) — CI·서명 주체 |
| G0-23 | 결정론 차단 | CCJ 규칙(`1.5e-7`)과 규범 골든(`1e-07`)의 지수 표기가 상충 | **Accepted** | 아키텍트 | `docs/g0/G0-RESOLUTION-9.md` ④ · `docs/adr/0004-audit-integrity-and-failure.md` | 2026-08-24 | **되돌림 불가**. 승인 순서 권고상 **최우선**(`:683`, `:691`) |
| G0-24 | FSM 차단 | R1 규범의 AuditError 허용 상태 집합이 본문과 예시에서 다름 | Proposed | 작성 Claude · 승인 아키텍트 | `docs/g0/G0-RESOLUTION-9.md` ⑤ · `docs/adr/0001-fsm-turn-and-action.md` | — | ADR 0001 `:6` 이 G0-24 담당 명시. 되돌림 가능하나 골든 리플레이 픽스처 재생성 필요(`0001:9`) |
| G0-25 | Core 차단 | `action.hpp`/`budget.hpp`/`permission_gate.hpp` 등 계약 전문 부재 | **Accepted** | 아키텍트 | `include/cogito/{action,budget,tool,registry,policy}.hpp` | 2026-08-25 | **S2 착수 한정 승인**. 공개 헤더 계약 확정 |
| G0-26 | 결정론 차단 | registry/policy/config/model digest 의 포함 필드·누락값·enum 표현 미정의 | **Accepted** | 아키텍트 | `docs/g0/G0-RESOLUTION-9.md` ⑦ · `docs/adr/0004-audit-integrity-and-failure.md` | 2026-08-24 | **되돌림 불가**. ④ 승인 후에만 벡터 확정 가능(`:675`) |
| G0-27 | 안전 차단 | `output_schema` 의 compile 시점·검증 시점·실패 분류가 없음 | **Accepted** | 아키텍트 | `include/cogito/tool_schema.hpp` | 2026-08-25 | **S2 착수 한정 승인**. Freeze 시점 컴파일 및 런타임 fail-closed 검증 |
| G0-28 | 실행 차단 | 동기 Tool handler hard timeout 중단 수단 없음 — 미반환 시 AgentLoop 정지 | 미착수 | 미배정 | ADR `0005-timeout-retry-idempotency` (미작성) | — | ADR 0005 예약(`SKILL.md:17`) |
| G0-29 | 정책 차단 | `ExecutionMode` enum 이 선형 권한 순서가 아니어서 숫자 min/max 불가 | **Accepted** | 아키텍트 | `docs/g0/G0-RESOLUTION-9.md` ⑧ · `include/cogito/identity.hpp` | 2026-08-25 | **S2 착수 한정 승인**. ModeToMaxEffect 사상 및 상한 초과 Deny |
| G0-30 | 승인 차단 | `FindUsable() const` 가 조회 중 만료 판정하나 상태를 변경할 수 없음 | 미착수 | 미배정 | 미배정 | — | ADR 0003 과 인접하나 `SKILL.md` 매핑에 없음 — 배정 필요 |
| G0-31 | 승인 차단 | `gate_reentry_count` 증가 시점(초기 Ask/승인 Resume/재승인)이 없음 | Proposed | 작성 Claude · 승인 아키텍트 | `docs/g0/G0-RESOLUTION-9.md` ⑨ · `docs/adr/0001-fsm-turn-and-action.md` | — | ADR 0001 `:6` 이 G0-31 담당 명시. 되돌림 가능 |
| G0-32 | ABI 차단 | C ABI 단순 tool 등록 구조로 output schema·idempotency·상한·ID 전달 불가 | 미착수 | 미배정 | 미배정 | — | G0-01(ABI v1.1 단일 기준) 승인에 종속 — 선행 관계 |
| G0-33 | Web 차단 | `style-src 'self'` CSP 와 React Flow/Recharts inline style 호환성 미검증 | 미착수 | 미배정 | ADR `0009-web-trust-boundary` (미작성) | — | 브라우저 실측은 **Antigravity 소유**(`CLAUDE.md §8`). Claude 는 계약만 |

### 2-1. 상태 집계 (직접 계수)

| 상태 | 건수 | 해당 G0 |
| --- | --- | --- |
| `Proposed` | **9** | 01, 09, 17, 18, 19, 20, 21, 24, 31 |
| `Accepted` | **7** | 05, 10, 23, 25, 26, 27, 29 (2026-08-24/25 사람 승인 완료) |
| `Rejected` | **0** | — |
| `Out of Scope` | **0** | — |
| `미착수` | **17** | 02, 03, 04, 06, 07, 08, 11, 12, 13, 14, 15, 16, 22, 28, 30, 32, 33 |
| **합계** | **33** | |

### 2-2. 차단 등급별 집계

| 차단 등급 | 건수 | G0 | Resolved |
| --- | --- | --- | --- |
| 전체 차단 | 2 | 01, 02 | 0 |
| 안전 차단 | 4 | 03, 04, 05, 27 | 2 (G0-05, G0-27) |
| 감사 차단 | 1 | 06 | 0 |
| Web 차단 | 5 | 17, 18, 19, 20, 33 | 0 |
| Core 차단 | 2 | 09, 25 | 1 (G0-25) |
| ABI 차단 | 2 | 08, 32 | 0 |
| 결정론 차단 | 2 | 23, 26 | 2 (G0-23, G0-26) |
| 범위 차단 | 2 | 15, 16 | 0 |
| 승인 차단 | 2 | 30, 31 | 0 |
| 동시성 / Schema / 빌드 / 패키지 / Provider / OPC UA / 운영 / 품질 / FSM / 실행 / 정책 차단 | 각 1 (계 11) | 07 / 10 / 11 / 12 / 13 / 14 / 21 / 22 / 24 / 28 / 29 | 2 (G0-10, G0-29) |

---

## 3. G0 Exit Gate 충족 현황 (실측) 및 S0/S1/S2 제한 착수

`Cogito++_개발_작업체크리스트.md:116` — "위 6개 체크가 모두 완료되고 안전 차단 0건일 때만 S0 제품 코드 작업을 시작한다."

| # | 체크(체크리스트 줄) | 충족 | 실측 근거 |
| --- | --- | --- | --- |
| 1 | `:109` G0-01~33 각각에 owner·결정일·ADR/이슈 링크 배정 | **진행 중** | 7건 Accepted(2026-08-24/25 확정), 나머지 배정 진행 중 |
| 2 | `:110` 모든 "전체/안전/감사 차단" 항목이 `Resolved` | **진행 중** | G0-05, G0-27 Resolved 완료, 나머지 5건 대기 |
| 3 | `:111` 보류 항목이 기본 OFF 이고 배포물·SBOM·마케팅 범위에서 제외 | **미충족** | 빌드 시스템 구축 시 검증 |
| 4 | `:112` 결정 결과를 명세 후속 버전 또는 승인된 ADR 에 역반영 | **일부 충족** | ADR-0004 Accepted 완료 |
| 5 | `:113` `docs/traceability.md` 에 요구사항→명세→ADR→작업→테스트 연결 | **미충족** | 후속 티켓 작성 대기 |
| 6 | `:114` 아키텍트·안전 책임자·보안 책임자·제품 책임자 승인 | **일부 충족** | G0-05, 10, 23, 25, 26, 27, 29 아키텍트 1차 승인 완료 |

> **[S0/S1 및 S2 제한 착수 허가 (2026-08-25)]**:
> 전체 G0 Exit Gate 6개 체크는 아직 미충족 상태이나, 사람(아키텍트/안전책임자)의 승인으로 핵심 7건(G0-05, G0-10, G0-23, G0-25, G0-26, G0-27, G0-29) 및 ADR-0004가 승인됨에 따라, **S0(빌드 시스템), S1(CCJ v1 / LP Digest / ID) 및 S2(Action / Tool Schema / Tool Contract / Registry / Config / Policy / Clock / SecretString) 코어 작업에 한해 제한적 착수가 허가**되었다.

---

## 4. ADR 번호 배정과 발견된 불일치

### 4-1. 예약된 ADR 번호

`Cogito++_구현_요구사항.md:583-591` 의 디렉터리 트리가 **0001~0008** 을 예약한다.
`0009-web-trust-boundary` 는 그 트리에는 없고 `Cogito++_개발_작업체크리스트.md:179`, `:1281` 과
`.claude/skills/adr-draft/SKILL.md:21` 에서만 요구된다.

| ADR | 파일명(예약) | `SKILL.md` 의 G0 매핑 | 실제 파일 | 상태 |
| --- | --- | --- | --- | --- |
| 0001 | `0001-fsm-turn-and-action.md` | G0-24, G0-31 (`SKILL.md:13`) | 존재 | `Proposed` (`docs/adr/0001-fsm-turn-and-action.md:3`) |
| 0002 | `0002-build-time-adapters.md` | G0-11, G0-12 (`:14`) | 없음 | 미작성 |
| 0003 | `0003-approval-and-identity.md` | G0-02, G0-03 (`:15`) | 없음 | 미작성 |
| 0004 | `0004-audit-integrity-and-failure.md` | **G0-06, G0-21** (`:16`) | 존재 | `Proposed` (`docs/adr/0004-audit-integrity-and-failure.md:3`) |
| 0005 | `0005-timeout-retry-idempotency.md` | G0-05, G0-28 (`:17`) | 없음 | 미작성 |
| 0006 | `0006-schema-dialect.md` | G0-10, G0-27 (`:18`) | 없음 | 미작성 |
| 0007 | `0007-external-data-boundaries.md` | — (`:19`) | 없음 | 미작성 |
| 0008 | `0008-packaging-and-airgap.md` | G0-22 (`:20`) | 없음 | 미작성 |
| 0009 | `0009-web-trust-boundary.md` | G0-17~G0-20, G0-33 (`:21`) | 없음 | 미작성. 요구사항 트리(`:583-591`)에는 부재 |

### 4-2. 불일치 1 — ADR 0004 의 담당 G0 가 두 문서에서 다르다 (지시된 확인 사항)

| 출처 | ADR 0004 가 담당한다고 적힌 G0 |
| --- | --- |
| `.claude/skills/adr-draft/SKILL.md:16` | **G0-06**(finalize/seal), **G0-21**(저장·마이그레이션) |
| `docs/adr/0004-audit-integrity-and-failure.md:6` | **G0-23**(CCJ 지수), **G0-05**(operation digest), **G0-26**(도메인 태그·projection) |

**실제 작성된 0004 는 CCJ·LP digest·감사 해시체인만 다루며 G0-06·G0-21 을 커버하지 않는다.**
(0004 의 제목 `CCJ v1 정규 직렬화, LP Digest, 감사 체인 무결성`, 근거 보고서 `docs/g0/G0-RESOLUTION-9.md` ② ④ ⑦ — `0004:8`)

파급:
- **G0-06 과 G0-21 은 어느 ADR 에도 실제로 귀속되어 있지 않다.** 이 대장에서 둘의 ADR 열을 `미배정`으로 둔 이유다.
- **G0-05 가 이중 배정이다.** `SKILL.md:17` 은 ADR 0005 소관이라 하고, 작성된 `0004:6` 은 0004 소관이라 한다.
- **G0-23·G0-26 은 `SKILL.md` 표에 아예 없다** — 0004 가 실제로 담당하는데 매핑표에 누락됐다.

→ **미결: `SKILL.md` 매핑표를 실제 0004 에 맞춰 고칠지, 0004 의 범위를 SKILL 매핑에 맞춰 좁힐지 결정 필요.**
`.claude/skills/**` 는 Claude 쓰기 가능 영역이나(`CLAUDE.md:2`), 이 대장 작업은 배정 파일 1개만 쓰므로 여기서는 기록만 한다.

### 4-3. 불일치 2 — ADR 0004 가 인용한 출처가 존재하지 않는다

`docs/adr/0004-audit-integrity-and-failure.md:12` 는
"파일명은 체크리스트 §13 의 `0004-audit-integrity-and-failure.md` 를 따른다" 고 적는다.
그러나 `Cogito++_개발_작업체크리스트.md` 의 §13 은 `:1181` **"S9 — OPC UA Adapter"** 이며,
해당 파일 전체에 문자열 `0004` 는 **0회 출현**한다(`grep -c` 실측).
실제 파일명 목록은 `Cogito++_구현_요구사항.md:583-591` 에 있다.

→ **0004 의 출처 인용은 정정 대상.** (명세 본문 수정 권한 없음 — 승인 후 반영)

### 4-4. 불일치 3 — 결정 기록 위치 `§16` 이 명세에 없다

`Cogito++_구현명세서.md:3025` 는 §12-14 미결 항목을 **"(§16에 추가)"** 하라고 하고,
`.claude/skills/adr-draft/SKILL.md:86` 도 "§16 외부 결정" 을 언급한다.
그러나 `Cogito++_구현명세서.md` 의 최상위 절은 **§13(`:3036` 기획안 수정 요청) 에서 끝나고 부록 A(`:3048`)·부록 B(`:3068`) 가 이어진다 — §14·§15·§16 은 존재하지 않는다.**

→ **미결: G0-13·14·16·17·22 의 제품 책임자 결정을 어디에 기록할지 정해야 한다.**
후보는 (a) 명세에 §16 신설, (b) `docs/adr/0009-web-trust-boundary.md`(G0-17 은 `체크리스트:89` 가 이미 지정), (c) 별도 결정표 문서. **제품 책임자·아키텍트 결정 필요.**

---

## 5. AI 가 정할 수 없는 5건

`docs/g0/G0-RESOLUTION-9.md:696-698` 이 명시한다.

| G0 | 주제 | 담당(승인권자) | 왜 AI 가 정할 수 없는가 |
| --- | --- | --- | --- |
| G0-13 | llama.cpp 공급망(반입 방식·commit SHA·해시 대상) | 제품 책임자 — **명세가 승인자를 지정하지 않음, 배정 필요** | 실제 공급망·법무·라이선스 실사가 필요하다 |
| G0-14 | OPC UA 실설비값(CAS 조건·타입·허용오차·timeout) | 제품 책임자 — **명세 미지정, 배정 필요** | 현장 설비 실측값이며 오추정 시 물리 안전에 직결된다 |
| G0-16 | 릴리스 범위(MQTT/RAG/C# 포함 여부) | 제품 책임자 — **명세 미지정, 배정 필요** | 제품 범위·배포 약속이다 |
| G0-17 | 인증원·step-up·SOD 예외·OT/IT 경계 | **제품 책임자** (`Cogito++_개발_작업체크리스트.md:89`, `Cogito++_구현명세서.md:3025`) | 사내 인증 인프라와 교대 운영 실태를 아는 사람만 정할 수 있다 |
| G0-22 | CI 공급자·릴리스 서명 주체 | 제품 책임자 — **명세 미지정, 배정 필요** | 서명 키 소유와 조직 책임 배분이다 |

**5건 모두 "AI 결정 불가".** 임의 기본값을 넣지 않는다.
`docs/g0/G0-17-QUESTIONNAIRE.md` 는 G0-17 에 대해 **질문지**를 제공할 뿐 결정을 대신하지 않는다.

---

## 6. 미결 목록 (이 대장으로 닫히지 않는 것)

| # | 미결 | 결정 주체 |
| --- | --- | --- |
| 1 | G0-04·07·08·15·30·32 의 ADR 번호 배정(예약표에 대응 항목 없음) | 아키텍트 |
| 2 | ADR 0004 담당 G0 불일치(§4-2) — SKILL 매핑을 고칠지 0004 범위를 바꿀지 | 아키텍트 |
| 3 | G0-06·G0-21 의 ADR 귀속처(현재 어느 ADR 에도 실제로 없음) | 아키텍트 |
| 4 | G0-05 이중 배정(ADR 0004 vs 0005) 해소 | 아키텍트 |
| 5 | 제품 책임자 결정 기록 위치(§16 부재, §4-4) | 제품 책임자 + 아키텍트 |
| 6 | G0-10·18·19·20·21 초안의 **승인권자 배정**(`체크리스트:114` 4개 역할 중 미배정) | 아키텍트 |
| 7 | G0-13·14·16·22 의 **승인자 지정**(명세가 이름을 주지 않음) | 제품 책임자 |
| 8 | G0-25 종료 증거 `docs/contracts/core-v1.md`(`체크리스트:97`) 미생성 — 작성 주체·시점 | 아키텍트 |
| 9 | `docs/traceability.md`(`체크리스트:113`) 미생성 — Exit Gate 체크 5 | Claude(작성) + 아키텍트(승인) |
| 10 | 이 대장의 갱신 주기와 갱신 책임자 | 제품 책임자 |

---

## 7. 갱신 규칙

1. **상태를 `Accepted` 로 바꿀 수 있는 것은 사람뿐이다.** Claude 는 `Proposed` 까지만 쓴다(`.claude/skills/adr-draft/SKILL.md:27-28`).
2. `Accepted` / `Rejected` 로 바꿀 때 **결정일과 승인자를 같은 커밋에서 채운다.** 결정일이 빈 `Accepted` 는 무효로 본다.
3. 머리의 `작성 N / 승인 0 / 미착수 M` 은 §2-1 집계와 항상 일치해야 한다. 합계는 언제나 33이다.
4. 새 G0 를 추가하지 않는다. 이 대장은 `Cogito++_개발_작업체크리스트.md:71-105` 의 33건을 그대로 추적한다.
5. §3 Exit Gate 표는 **실측으로만** 갱신한다. 파일 존재 여부는 명령 출력으로 확인하고, 상태 문자열은 `파일:줄번호` 로 인용한다.
