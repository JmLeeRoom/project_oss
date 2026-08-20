# Cogito++ 개발 실행 체크리스트

**문서 상태**: 구현 준비용 기준서 — G0 차단 결정 해소 전 제품 코드 착수 금지  
**작성 기준일**: 2026-08-20  
**분석 원본**: `Cogito++_구현명세서.md` v1.0, 3,108행, 165,336바이트  
**원본 SHA-256**: `4C3CB8C8A888083C36CB2A380AED45B593560481AE848D2E23CAC7D950F7AAD7`  
**적용 범위**: 저장소 초기화부터 Core, 감사·승인, C ABI, Provider, OPC UA, Web, 검증, 패키징, 현장 인수까지  
**우선순위**: 이 체크리스트와 원본 명세가 충돌하면 원본 명세와 승인된 ADR을 우선한다. 원본 명세에서 정의되지 않았거나 서로 충돌하는 값은 구현하지 않고 G0/ADR에서 먼저 확정한다.

---

## 1. 이 문서 사용법

이 문서는 작업 순서를 강제하는 실행형 체크리스트다. 상위 단계의 **Exit Gate**가 통과되지 않으면 다음 단계로 넘어가지 않는다.

### 1-1. 체크 규칙

- [ ] 상위 항목은 모든 하위 항목, 테스트, 검토, 증거가 끝난 뒤에만 체크한다.
- [ ] 구현만 끝나고 테스트가 없으면 미완료다.
- [ ] 테스트가 통과해도 요구사항-코드 추적 링크와 검증 증거가 없으면 미완료다.
- [ ] `skip`, `xfail`, 비활성화된 테스트는 통과로 계산하지 않는다. 불가피하면 승인된 이슈와 만료일을 연결한다.
- [ ] 안전·보안 실패 케이스는 “오류 반환”뿐 아니라 **상태 불변**과 **write 호출 0회**를 함께 확인한다.
- [ ] 결정론 관련 테스트는 한 플랫폼 통과로 끝내지 않고 MSVC, GCC, Clang에서 바이트 단위 결과를 비교한다.
- [ ] 외부 연동 테스트는 정상 경로뿐 아니라 timeout, disconnect, duplicate, stale, malformed, 재시작을 포함한다.
- [ ] 명세에 없는 값을 발견하면 임의 기본값을 넣지 않는다. ADR을 작성하고 원본 명세 또는 후속 명세에 역반영한다.

### 1-2. 작업 티켓에 반드시 남길 정보

각 티켓/PR 본문에 아래 필드를 복사한다.

```text
Task ID:
원본 근거: Cogito++_구현명세서.md §...
선행 작업:
변경 파일:
구현 범위:
비범위:
실패 모드:
검증 명령:
테스트 결과:
안전 영향(write 0회 근거 포함):
산출물/로그 위치:
검토자:
```

### 1-3. 공통 완료 정의(Definition of Done)

- [ ] 공개 계약은 헤더 주석, 구현, 테스트가 같은 의미를 가진다.
- [ ] 모든 새 오류 경로에 안정적인 `Errc`와 `reason_code`가 있으며 표시 문자열로 분기하지 않는다.
- [ ] 입력 크기, 개수, 깊이, 시간, 큐 길이 중 적용 가능한 상한이 명시되고 경계값 테스트가 있다.
- [ ] 외부 문자열과 비밀값이 감사, Ops 로그, UI 권위 영역에 유입되지 않는 테스트가 있다.
- [ ] write 가능 경로는 Gate → verdict 감사 → `tool_call_started` 감사 → Permit → Invoker 순서를 벗어날 수 없다.
- [ ] 예외가 C ABI를 넘지 않고, 프로세스 중단이 허용된 유일한 경로는 승인된 봉인 실패 정책과 일치한다.
- [ ] 정적 분석 경고 0, 해당 sanitizer 오류 0, 관련 fuzz seed 회귀 0이다.
- [ ] 변경된 계약의 골든 fixture 버전과 digest가 갱신되며, 이유가 리뷰에 남는다.
- [ ] 설치 트리와 소비자 프로젝트에서도 빌드되며 소스 트리의 우연한 include/link에 의존하지 않는다.
- [ ] 문서, SBOM, 제3자 고지, 운영 Runbook 중 영향을 받는 항목이 함께 갱신된다.

---

## 2. 분석 결론과 착수 전 차단사항

### 2-1. 현재 저장소 상태

- 분석 시점 저장소에는 구현 소스와 빌드 파일이 없고 Markdown 문서만 있다. 아래 파일은 모두 신규 생성 대상으로 본다.
- 원본 명세는 파일 배치, 핵심 C++ 계약, 19개 FSM 전이, Gate 1~7, 감사 DDL, ABI, Web 신뢰 경계까지 상세히 정의한다.
- 그러나 아래 항목은 원본 내부 계약이 서로 맞지 않거나 구현에 필요한 값이 빠져 있다. “구현 착수 가능” 표기와 관계없이 먼저 닫아야 한다.

### 2-2. G0 결정 대장

| ID | 차단 수준 | 발견 내용 | 권장 확정 방향 | 종료 증거 |
| --- | --- | --- | --- | --- |
| G0-01 | 전체 차단 | ABI v1.0과 v1.1이 혼재하고 예외 래퍼 예시는 이미 요청별 `Subject` 시그니처를 사용한다 | 제품 최초 구현 기준을 ABI v1.1로 고정하고 v1.0은 역사적 설명으로만 남긴다. 호환 심볼이 필요하면 별도 표로 정의한다 | ADR + 최종 `cogito.h` 함수 목록 |
| G0-02 | 전체 차단 | `AgentLoop`는 생성 시 고정 `Subject`와 `RunTurn(string)`을 정의하지만 ABI/Web은 요청마다 주체를 전달한다 | `RunTurn`, 재개, 취소, 승인, ack, 복구 명령의 주체 전달·보존 규칙을 Core 계약부터 요청별 모델로 통일한다 | Core 시퀀스 다이어그램 + 헤더 계약 테스트 |
| G0-03 | 안전 차단 | `ApprovalRecord`에 nonce는 있으나 `Respond`가 nonce를 받지 않고, requester가 없어 자기 승인 분리를 검사할 수 없다 | requester/approver `Subject`, nonce, action digest, scope digest를 응답 시 모두 대조하고 단일 사용 상태 전이를 정의한다 | 승인 상태표 + 위변조/자기 승인 테스트 목록 |
| G0-04 | 안전 차단 | §6-4는 indeterminate 후 동일 작업을 `Ask` 이상으로 강등한다고 하나 Gate 의사코드는 즉시 `Deny(indeterminate_lockdown)`하여 승인 경로에 도달하지 않는다 | 정책 Deny는 유지하되 lockdown은 강제 승인 플래그로 전달할지, 영구 Deny할지 ADR로 하나만 선택한다. 원문의 운영자 승인·ack 흐름을 살리려면 강제 `Ask`가 일관된다 | 수정된 Gate 의사코드 + 행렬 테스트 |
| G0-05 | 안전 차단 | 동일 `(tool_name, canonical_arguments)` 잠금 키에 `action_digest`를 쓰지만 action digest에는 session/turn/action id가 포함되어 재요청마다 달라진다 | 별도 `operation_digest = LP(domain, tool_name, CCJ(arguments))`를 정의한다 | 고정 digest 벡터 + 다른 turn에서 잠금 유지 테스트 |
| G0-06 | 감사 차단 | `RetryFinalize()`가 원래 `TurnOutcome` 대신 새 Failed outcome으로 `turn_end`를 만들 수 있다 | 최초 finalize 시 완성된 immutable `pending_turn_end` 이벤트를 보관하고 같은 event/payload를 멱등 재커밋한다 | 실패→재시도 전후 payload byte 일치 테스트 |
| G0-07 | 동시성 차단 | 모든 명령은 AgentLoop 전용 큐를 거쳐야 하지만 실행 중에는 cancel 명령을 dequeue할 수 없다 | `request_cancel`은 `@thread:any` 원자 플래그 설정만 수행하고, 전이·감사는 owner thread가 poll 지점에서 처리하도록 순서와 latency 상한을 확정한다 | thread model ADR + blocking tool 취소 테스트 |
| G0-08 | ABI 차단 | `agent_destroy`가 `void`인데 wrong-thread 오류 반환 요구가 있으며 `agent_create`, `verify_audit_chain` 등의 `@thread` 분류가 빠져 있다 | 모든 공개 함수에 친화성을 전수 표기하고 반환형/파괴 정책을 일관되게 수정한다 | 헤더 정적 검사 통과 |
| G0-09 | Core 차단 | 명세는 내부 오류 반환에 `Error`와 `Result<T>`를 쓰지만 무값 성공을 위한 `Result<void>` 및 예외 허용 범위가 없다 | 무값 API는 `Error`로 통일하거나 `Result<void>` 특수화를 정의하고, 내부 예외/`noexcept`/OOM 정책을 ADR로 고정한다 | 컴파일 계약 테스트 + 예외 정책 ADR |
| G0-10 | Schema 차단 | 런타임 pattern 200ms 상한이 필수이나 C++17 표준 정규식에는 안전한 취소/timeout이 없다 | 정규식 엔진, 지원 문법, 자원 제한, timeout 구현을 선택한다. runaway worker를 남기는 단순 `async` 방식은 금지한다 | 엔진 선택 ADR + catastrophic fixture 테스트 |
| G0-11 | 빌드 차단 | vcpkg `web` feature의 자기 패키지 의존, overrides/baseline 조합, 선택 포트 존재 여부가 실제 baseline에서 검증되지 않았다 | baseline SHA를 먼저 고정하고 manifest install/dry run으로 자기 feature 표현을 검증한다. 불가하면 web의 직접 의존성을 명시한다 | lock된 baseline + 전 feature 해석 로그 |
| G0-12 | 패키지 차단 | CMake 예시 그대로면 설치 target 이름이 `cogito::cogito_core`가 되어 빌드 트리 alias `cogito::core`와 다를 수 있고, 선택 target 수집 시점도 불명확하다 | `EXPORT_NAME`, target 정의 순서, feature→target 연결을 고정하고 설치 소비자 테스트로 판정한다 | clean prefix consumer 테스트 |
| G0-13 | Provider 차단 | llama.cpp 반입/링크 방식, commit SHA, tokenizer digest 대상 바이트와 chat template 추출 규칙이 없다 | source/submodule/package 중 하나와 해시 대상 파일을 ADR로 고정한다 | 공급망 manifest + 변조 실패 테스트 |
| G0-14 | OPC UA 차단 | CAS 가능 조건, NodeId/Variant 타입, 허용오차, read-back timeout, manifest 필드가 없다 | OPC UA 작업 계약과 매니페스트 schema를 먼저 작성하고 지원하지 않는 타입은 fail-closed 한다 | ADR + schema + mock server 계약 테스트 |
| G0-15 | 범위 차단 | §1은 MCP §10-3을 참조하지만 실제 §10-3이 없고 구현 파일/단계도 없다 | MVP 포함 여부를 결정한다. 포함 시 discover/re협상/transport/감사 계약을 별도 절로 작성한다 | 범위 승인 + MCP 명세 보완 또는 명시적 제외 |
| G0-16 | 범위 차단 | MQTT/RAG는 빌드 옵션만 있고 계약·테스트가 없으며 C#은 폴더와 OPC UA 시연 요구만 있다 | 현재 릴리스 포함 범위를 표로 확정한다. 상세 계약 없는 adapter는 OFF/미배포한다. C#은 최소 P/Invoke smoke 범위를 정의한다 | 릴리스 scope matrix |
| G0-17 | Web 차단 | 인증원/step-up/승인 역할/SOD 예외/OT-IT 범위/브라우저/감사 필드가 §12-14에 미결이다 | 제품 책임자가 실제 현장값을 승인하고 `0009-web-trust-boundary`에 기록한다 | 서명된 제품 결정표 |
| G0-18 | Web 차단 | 로그인·로그아웃, 세션 쿠키, CSRF 토큰 발급·회전, OIDC/배지 bootstrap 경로가 없다 | 인증 모드별 bootstrap과 쿠키 속성, TTL/회전/폐기, CSRF 수명주기를 API 계약에 추가한다 | auth sequence + 공격 테스트 |
| G0-19 | Web 차단 | POST/GET body schema, 공통 오류 envelope/status mapping, command cache TTL/퇴거/재시작, turn outcome 보존 상한이 없다 | 모든 경로의 JSON Schema와 HTTP 의미론, 멱등/보존 정책을 고정한다 | OpenAPI 또는 동등 계약 + contract tests |
| G0-20 | Web 차단 | `assets_digest`를 Web Host가 Core `turn_begin`/state에 전달하는 ABI/config 진입점이 없다 | 생성·검증·주입 시점과 불일치 시 기동 실패 규칙을 ABI/config에 추가한다 | tampered asset boot-fail 테스트 |
| G0-21 | 운영 차단 | SQLite schema migration/versioning, WAL/디스크 임계값, 보존·백업·복구 정책이 없다 | 최초 DDL과 함께 migration 및 용량 운영 계약을 확정한다 | migration test + audit runbook |
| G0-22 | 품질 차단 | CI 공급자, 최소/권장 compiler patch, ARM64 native/cross 방식, release signing 주체가 없다 | 지원 플랫폼 표와 CI/release 책임자를 승인한다 | toolchain matrix + release policy |
| G0-23 | 결정론 차단 | CCJ 규칙은 지수 선행 0을 제거하고 `1.5e-7`로 쓰라 하지만 규범 골든은 `1e-07`이다 | 어떤 표기를 권위로 삼을지 결정하고 규칙·24개 벡터·serializer를 하나로 맞춘다 | 수정 명세 + 3 compiler byte 비교 |
| G0-24 | FSM 차단 | R1 규범은 `Infer`~`Observe`에만 AuditError를 허용하지만 예시 `ResolveUniversal`은 Idle을 포함한 모든 비종료 상태에 적용한다 | 정확한 R1 상태 집합을 고정하고 `ResetForNextTurn`의 직접 상태 대입도 정상 경로에서 제거한다 | 상태×이벤트 전수 표 |
| G0-25 | Core 차단 | `action.hpp`, `budget.hpp`, `permission_gate.hpp`, `conversation.hpp`, `ops_log.hpp`와 `GateInput`, `Limits`, `Deadline`, `CancelToken`의 계약 전문이 없다 | 필드, 상한, lifetime, 상태 변경 시점, thread ownership을 헤더 구현 전에 계약 문서로 고정한다 | `docs/contracts/core-v1.md` |
| G0-26 | 결정론 차단 | registry/policy/config/model digest의 정확한 포함 필드, 누락값 표현, enum 표현이 모두 정의되지 않았다 | handler 포함 여부를 포함해 digest projection과 이름순 정렬을 field-by-field로 정의한다 | projection 표 + 고정 벡터 |
| G0-27 | 안전 차단 | `output_schema`는 descriptor에 있으나 compile 시점, Invoker 검증 시점, 실패 결과 분류가 없다 | Registry Freeze에서 입력·출력 schema를 함께 compile하고 handler 반환 직후 크기/JSON/output schema를 검사하도록 고정한다 | invalid output write 결과 테스트 |
| G0-28 | 실행 차단 | 동기 Tool handler의 hard timeout을 어떻게 중단할지 없고 반환하지 않으면 AgentLoop가 멈춘다 | cooperative cancellation만 지원할지 격리 worker/process를 둘지 정하고, side-effect 작업의 강제 중단 결과를 indeterminate와 연결한다 | timeout ADR + hung tool test |
| G0-29 | 정책 차단 | `ExecutionMode` enum은 선형 권한 순서가 아니므로 숫자 `min/max`로 상한을 계산할 수 없다 | 4 mode × 3 effect의 명시 행렬과 요청 mode/config mode 결합 규칙을 정의한다 | table-driven mode tests |
| G0-30 | 승인 차단 | `FindUsable() const`가 조회 중 만료를 판정한다고 하나 상태와 `pending_count_`를 변경할 수 없다 | 만료 mutation은 owner-thread `ExpireOlderThan`으로만 수행할지 `FindUsable`을 비const로 할지 고정한다 | 승인 상태 전이표 |
| G0-31 | 승인 차단 | `gate_reentry_count`를 초기 Ask, 승인 Resume, 정책 변경 후 재승인 중 언제 증가시키는지 없다 | action별 카운터 증가·초기화 시점을 상태표로 정의하고 “두 번째 Ask Deny”를 예제로 고정한다 | 3회 연속 시퀀스 테스트 |
| G0-32 | ABI 차단 | C ABI의 단순 tool 등록 구조로 output schema, idempotency, output 상한, provider/invoker ID를 모두 전달할 수 없다 | v1.1 descriptor struct/JSON registration 계약과 `struct_size` 확장 규칙을 정의한다 | C registration round-trip test |
| G0-33 | Web 차단 | `style-src 'self'` CSP와 React Flow/Recharts가 생성할 수 있는 inline style의 실제 호환성이 검증되지 않았다 | production bundle을 최종 CSP로 조기 검증하고, 약화 없이 불가하면 권위 전이표 기반 정적 SVG로 전환한다 | browser CSP violation 0 |

### 2-3. G0 종료 체크리스트

- [ ] G0-01~G0-33 각각에 owner, 결정일, ADR/이슈 링크를 배정했다.
- [ ] 모든 “전체/안전/감사 차단” 항목이 `Resolved`다.
- [ ] 보류 항목은 해당 feature가 기본 OFF이고 배포물·SBOM·마케팅 범위에서 제외됨을 확인했다.
- [ ] 결정 결과를 `Cogito++_구현명세서.md` 후속 버전 또는 승인된 ADR에 역반영했다.
- [ ] `docs/traceability.md`에 요구사항 → 명세 절 → ADR → 작업 ID → 테스트 ID 연결을 만들었다.
- [ ] 아키텍트, 안전 책임자, 보안 책임자, 제품 책임자가 각자 관련 결정을 승인했다.

**G0 Exit Gate**: 위 6개 체크가 모두 완료되고 안전 차단 0건일 때만 S0 제품 코드 작업을 시작한다.

---

## 3. 단계와 의존 관계

```text
G0 명세 충돌 해소
  └─ S0 계약·저장소·빌드 고정
      └─ S1 결정론 기반(CCJ·Digest·ID·Clock)
          └─ S2 Tool Schema·Registry·Config
              └─ S3 FSM
                  └─ S4 Policy·Budget·Approval·Permit
                      └─ S5 Permission Gate·Audit
                          └─ S6 Conversation·Inference·Invoker·AgentLoop
                              └─ S7 C ABI v1.1·CLI·설치 패키지
                                  ├─ S8 HTTP/llama.cpp Provider
                                  ├─ S9 OPC UA Adapter
                                  └─ S10 Web Host·Dashboard
                                      └─ S11 통합 보증·릴리스·현장 인수
```

- S8, S9, S10은 S7 이후 병렬 개발할 수 있으나 같은 Core/ABI 계약을 변경하면 다시 S7 회귀 테스트를 통과해야 한다.
- S10은 S7 완료만으로 시작할 수 없다. ADR 0009 승인, Web 위협 모델, G0-17~G0-20 해소가 모두 필요하다.
- MQTT, RAG, MCP 전체 구현은 G0-15/G0-16에서 포함으로 승인되지 않으면 이 체크리스트의 릴리스 범위 밖이다.

---

## 4. S0 — 계약 고정, 저장소 골격, 빌드 기반

### S0-01. 릴리스 범위와 불변식 고정

- [ ] MVP/P1/P2/Web/후속 릴리스의 feature 표를 작성한다.
- [ ] 기본 ON/OFF feature와 실제 배포 대상 library/executable을 연결한다.
- [ ] 다음 불변식을 `docs/architecture-invariants.md`에 번호로 고정한다.
  - [ ] 유효한 미소비 Permit 없이는 Tool handler에 도달하지 않는다.
  - [ ] 미등록과 tombstone 도구는 정책이 Allow여도 Deny다.
  - [ ] GBNF 적용 여부와 무관하게 모든 arguments를 런타임 재검증한다.
  - [ ] 판정 불가, 계약 불일치, 감사 실패는 fail-closed다.
  - [ ] 승인은 action/scope/policy/registry/session/turn/nonce/주체에 결합되고 단일 사용이다.
  - [ ] 한 inference 응답은 Action 0개 또는 1개만 허용한다.
  - [ ] write 전에 verdict와 `tool_call_started`가 내구성 있게 커밋된다.
  - [ ] write 전 감사 실패 시 handler 호출은 0회다.
  - [ ] 실행 중 취소된 side-effect 작업은 성공/실패가 아니라 indeterminate로 다룬다.
  - [ ] Tool/RAG/MCP 텍스트는 `untrusted`이며 승인 권위 영역에 들어가지 않는다.
  - [ ] AgentLoop의 상태 변경은 owner thread 하나에서 직렬화한다.
  - [ ] 모든 턴은 `turn_end` 정확히 1개 또는 명시적 finalize-pending/sealed 상태를 가진다.
  - [ ] Cogito++는 기능 안전 PLC/interlock을 대체하지 않는다.
- [ ] 불변식별 정적 방어, 런타임 방어, 테스트, 운영 감시 항목을 표로 연결한다.

### S0-02. ADR 작성 및 승인

- [ ] `docs/adr/`와 ADR 템플릿을 만든다.
- [ ] ADR 상태를 `Proposed / Accepted / Superseded / Rejected`로 정의한다.
- [ ] 최소 ADR 0001~0008의 제목, 소유자, 포함 결정을 먼저 매핑한다.
- [ ] `0001`에 FSM 전이, R1/R2/R3, 재진입, commit-before-transition를 기록한다.
- [ ] CCJ v1/LP digest/domain tag/골든 벡터 결정을 ADR로 기록한다.
- [ ] Tool Schema Tier-G/Tier-V/pattern engine/fail-closed 결정을 ADR로 기록한다.
- [ ] 승인 단일 사용, nonce, SOD, indeterminate, finalize/seal을 ADR로 기록한다.
- [ ] Provider 단일 Action, llama.cpp 공급망/문법 실패 정책을 ADR로 기록한다.
- [ ] C ABI v1.1 소유권, thread affinity, exception boundary를 ADR로 기록한다.
- [ ] vcpkg/CMake/폐쇄망/제3자 코드 정책을 ADR로 기록한다.
- [ ] OPC UA write/CAS/read-back/indeterminate 계약을 ADR로 기록한다.
- [ ] Web 착수 전 `0009-web-trust-boundary`를 별도로 승인한다.
- [ ] ADR마다 대안, 기각 이유, 안전 영향, 롤백/마이그레이션 영향을 적는다.

### S0-03. 저장소 골격 생성

- [ ] 원본 §2의 `include/cogito`, `src`, `config`, `tests`, `tools`, `bindings`, `docs`, `cmake` 구조를 만든다.
- [ ] 빈 디렉터리를 유지할 목적의 placeholder 대신 실제 최소 README 또는 첫 파일을 둔다.
- [ ] `.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`, `.clang-tidy`를 추가한다.
- [ ] 텍스트 LF, UTF-8, generated asset, binary fixture 정책을 `.gitattributes`에 고정한다.
- [ ] `LICENSE`, `NOTICE`, `THIRD_PARTY_LICENSES.md`, `SECURITY.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`를 준비한다.
- [ ] 보안 취약점 비공개 신고 경로와 지원 버전 정책을 `SECURITY.md`에 적는다.
- [ ] `src/fakes`는 제품 `cogito_core`가 아니라 별도 `cogito_test_support` target으로 분리할지 G0 결정에 따라 구성한다.
- [ ] generated 파일(`assets_embedded.cpp`, SBOM, golden output)의 생성기와 source-of-truth를 주석으로 명시한다.

### S0-04. Toolchain과 preset 고정

- [ ] C++17, extensions OFF, PIC, hidden visibility를 전 target 공통값으로 적용한다.
- [ ] 지원 최소 compiler를 MSVC 2019 16.11+, GCC 9+, Clang 12+로 확정하거나 G0-22 결정으로 갱신한다.
- [ ] 실제 CI compiler patch version과 표준 library version을 기록한다.
- [ ] `CMakePresets.json`에 최소 `windows-msvc-debug`, `linux-gcc-debug`, `linux-clang-asan`, `linux-clang-tsan`, `linux-release`, `linux-arm64-release`를 정의한다.
- [ ] single-config/multi-config preset의 build directory가 충돌하지 않게 분리한다.
- [ ] warning-as-error는 프로젝트 코드에만 적용하고 외부 헤더에는 적용하지 않는다.
- [ ] MSVC `/permissive- /W4`, GCC/Clang `-Wall -Wextra -Wpedantic`와 프로젝트 추가 경고를 `CogitoWarnings.cmake`에 둔다.
- [ ] Release에서도 assertions에 의존하지 않는 런타임 검사를 유지한다.
- [ ] ASan/UBSan/TSan은 서로 호환되는 별도 preset으로 둔다.

### S0-05. vcpkg 공급망 고정

- [ ] `builtin-baseline`을 실제 40자리 commit SHA로 바꾼다.
- [ ] `vcpkg-configuration.json`의 registry URL과 baseline commit을 고정한다.
- [ ] `nlohmann-json 3.11.3`, `json-schema-validator 2.4.0` override가 해당 baseline에서 해석되는지 확인한다.
- [ ] `audit`, `http`, `http-curl`, `opcua`, `mqtt`, `rag`, `tests`, `web` feature를 각각 독립 해석한다.
- [ ] `open62541[openssl]`, `cpp-httplib[openssl]`, `curl[ssl]`, `paho-mqttpp3`, SQLite `fts5/json1` feature 이름을 baseline에서 재검증한다.
- [ ] G0-11에 따라 web 자기 의존 표현을 수정하거나 검증 결과를 고정한다.
- [ ] binary cache/사내 registry/air-gap mirror의 생성, 서명, 반입 절차를 문서화한다.
- [ ] direct/transitive dependency의 버전, license, source hash를 초기 SBOM seed로 저장한다.
- [ ] `picosha2.h` 원본 URL, commit/tag, SHA-256, MIT 고지를 기록하고 파일 내 저작권을 보존한다.

검증 예시:

```powershell
vcpkg x-update-baseline --dry-run
vcpkg install --dry-run --x-manifest-root=.
vcpkg install --dry-run --x-manifest-root=. --x-feature=tests --x-feature=http --x-feature=opcua
```

### S0-06. CMake 최소 골격과 테스트 harness

- [ ] root `CMakeLists.txt`에 project/version/options/dependency discovery를 정의한다.
- [ ] optional target을 모두 정의한 뒤 install/export 대상 목록을 만든다.
- [ ] build-tree alias와 install-tree exported name이 동일하도록 `EXPORT_NAME`을 설정한다.
- [ ] C ABI shared library와 static core의 compile definition/visibility를 분리한다.
- [ ] `include/`만 PUBLIC이고 `src/third_party`는 PRIVATE인지 확인한다.
- [ ] Catch2 v3 기반 test target과 `ctest` 등록 helper를 만든다.
- [ ] production library에서 test fake와 fuzz harness가 링크되지 않는지 map 파일로 확인한다.
- [ ] 빈 smoke library/executable로 모든 preset의 configure/build/test가 성공하게 한다.
- [ ] install prefix에 설치한 뒤 별도 source tree에서 `find_package(cogito CONFIG REQUIRED)` smoke를 수행한다.

검증 예시:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --config Debug
ctest --preset windows-msvc-debug -C Debug --output-on-failure
cmake --install build/windows-msvc-debug --config Debug --prefix build/install-smoke
```

### S0-07. CI 골격과 추적성

- [ ] PR 필수 job을 format, configure, build, unit, install-consumer, license로 분리한다.
- [ ] main/nightly job에 ASan/UBSan, TSan, fuzz smoke, ARM64, air-gap rebuild, SBOM을 둔다.
- [ ] test 이름을 `canonical.*`, `core.*`, `audit.*`, `replay.*`, `abi.*`, `web.*`, `fuzz.*`로 고정한다.
- [ ] CI가 생성한 compiler version, vcpkg baseline, config/policy/registry digest를 artifact에 남긴다.
- [ ] `docs/traceability.md` 누락을 검사하는 스크립트를 CI에 연결한다.
- [ ] 공개 헤더에 `@thread` 주석이 없는 ABI 선언을 실패시키는 검사 자리를 만든다.
- [ ] branch protection에서 안전 필수 job 우회를 제한한다.

**S0 Exit Gate**

- [ ] G0 차단 0건이다.
- [ ] 모든 preset이 최소 smoke configure/build/test를 통과한다.
- [ ] 고정 baseline으로 네트워크 없는 재설치가 가능하다.
- [ ] ADR 0001~0008이 승인 상태다.
- [ ] 설치 소비자 smoke가 source tree 밖에서 성공한다.

---

## 5. S1 — 결정론 기반: Result, ID, Clock, CCJ v1, Digest

### S1-01. 오류와 결과 계약

- [ ] `include/cogito/result.hpp`에 전체 `Errc`, 안정 `reason_code`, `Error`, `Result<T>`를 정의한다.
- [ ] 기본 `Verdict`와 오류 기본값이 fail-closed인지 확인한다.
- [ ] 성공 객체에서만 value 접근, 실패 객체에서만 error 접근이 가능하도록 계약을 구현한다.
- [ ] move-only 타입도 `Result<T>`에 담을 수 있게 한다.
- [ ] G0-09에서 정한 무값 결과와 예외/OOM 정책을 적용한다.
- [ ] 오류의 사용자 메시지와 상세 진단을 분리하고 secret/arguments 원문을 detail에 자동 포함하지 않는다.
- [ ] `Error::Ok()`와 실제 오류를 bool로 혼동하지 않는 단위 테스트를 둔다.
- [ ] 잘못된 value/error 접근이 assert 의존 없이 정의된 동작을 갖는지 확인한다.

### S1-02. ID와 UUID

- [ ] `SessionId`, `TurnId`, `ActionId`, `Digest`, UUID 타입과 비교/직렬화 규칙을 구현한다.
- [ ] UUIDv4의 version/variant bit와 난수 실패 처리를 테스트한다.
- [ ] digest는 정확히 32바이트이며 lowercase hex 왕복을 제공한다.
- [ ] 잘못된 길이, 대문자 허용 여부, 비hex 문자를 명시적으로 거부한다.
- [ ] 빈 action id가 허용되는 유일한 이벤트 범위를 문서화한다.
- [ ] ID 생성 충돌/실패를 조용히 대체하지 않고 startup/runtime 오류로 전달한다.

### S1-03. Clock과 프로세스 epoch

- [ ] `Clock`, `SystemClock`, `FakeClock`을 구현한다.
- [ ] UTC는 RFC3339 형식, duration/expiry는 monotonic nanoseconds만 사용한다.
- [ ] 프로세스 시작마다 새 `process_epoch_id`를 생성하고 감사 event에 넣는다.
- [ ] wall clock 역행이 TTL/deadline 판단에 영향을 주지 않는 테스트를 만든다.
- [ ] FakeClock은 wall/monotonic 이벤트 순서를 fixture로 완전 제어할 수 있어야 한다.
- [ ] monotonic 값 비교는 같은 epoch 안에서만 수행한다.
- [ ] overflow/음수 duration/극단 TTL 경계를 검사한다.

### S1-04. Strict JSON parser

- [ ] `ccj::ParseStrict`에 기본 상한 `max_bytes=256KiB`, `max_depth=32`, `max_object_keys=512`를 적용한다.
- [ ] UTF-8 유효성, 중복 key, 깊이, 전체 크기, key 수를 파싱 단계에서 거부한다.
- [ ] 중복 key가 nlohmann/json 객체로 덮어쓰기 되기 전에 SAX/parser hook에서 탐지되게 한다.
- [ ] invalid UTF-8, overlong sequence, surrogate, truncated sequence fixture를 추가한다.
- [ ] 숫자 overflow, NaN, Infinity, trailing data, BOM, 잘못된 escape를 거부한다.
- [ ] array 순서와 입력 문자열 code point를 정규화 없이 보존한다.

### S1-05. CCJ v1 serializer

- [ ] 공백 없이 `,`와 `:`만 사용한다.
- [ ] object key를 UTF-16 code unit 오름차순으로 정렬한다.
- [ ] `"`, `\\`, U+0000~001F escape와 소문자 `\\u00xx` 규칙을 구현한다.
- [ ] 비제어 Unicode는 UTF-8 원문을 유지하고 Unicode 정규화를 수행하지 않는다.
- [ ] `-0.0`과 범위 내 정수형 double을 정수로 출력한다.
- [ ] `%.15g → %.16g → %.17g` bit-identical round-trip 선택과 C locale 고정을 구현한다.
- [ ] 지수 `e`, 부호, 선행 0 규칙을 원본 24개 벡터와 byte compare한다.
- [ ] digest 입력에서 `json.dump()` 직접 사용을 정적 검색/코드리뷰 규칙으로 금지한다.
- [ ] `SelfTest()`를 startup 순서에 연결하고 한 벡터라도 실패하면 프로세스 시작을 막는다.

### S1-06. SHA-256와 LP DigestBuilder

- [ ] vendored picosha2 wrapper가 raw bytes를 정확히 처리하고 locale/endianness에 영향받지 않게 한다.
- [ ] 모든 field는 32-bit little-endian 길이 prefix 뒤에 bytes를 넣는다.
- [ ] 정수는 64-bit little-endian으로 넣는다.
- [ ] domain tag를 첫 field로 강제하고 action/permit/audit/operation 간 tag를 재사용하지 않는다.
- [ ] `ComputeActionDigest`에 session, turn, action, tool, CCJ arguments를 정확한 순서로 넣는다.
- [ ] `ComputePermitScopeDigest`에 action, subject, mode, policy, registry digest를 넣는다.
- [ ] `ComputeAuditHash`에 DDL의 모든 의미 필드와 canonical payload를 넣는다.
- [ ] G0-05에서 정한 operation digest를 별도 구현한다.
- [ ] `idempotency_key = lowercase_hex(action_digest)`를 단일 함수에서 만든다.
- [ ] 빈 문자열과 누락 field가 length prefix 때문에 충돌하지 않는 벡터를 추가한다.

### S1-07. 결정론 및 fuzz 검증

- [ ] `tests/canonical/ccj_golden.cpp`에 원본 24개 벡터를 상수로 둔다.
- [ ] `tests/canonical/digest_vectors.cpp`에 action/permit/audit/operation 고정 벡터를 둔다.
- [ ] 각 digest 입력 field를 하나씩 바꿨을 때 digest가 변하는 parameterized test를 만든다.
- [ ] MSVC/GCC/Clang 결과 파일의 SHA-256을 CI에서 직접 비교한다.
- [ ] locale을 한국어/독일어/C로 바꿔도 출력이 같은지 테스트한다.
- [ ] 32/64-bit 경계, ARM64 endianness 가정, signed conversion을 검사한다.
- [ ] `fuzz_ccj`에 malformed UTF-8, duplicate key, deep/large JSON, numeric boundary corpus를 넣는다.
- [ ] ASan/UBSan에서 최소 지정 시간 fuzz smoke를 통과한다.

검증 예시:

```powershell
ctest --preset windows-msvc-debug -C Debug -R "canonical" --output-on-failure
```

**S1 Exit Gate**

- [ ] CCJ 24개와 digest 고정 벡터가 3개 compiler 계열에서 byte-identical이다.
- [ ] startup self-test 실패 주입 시 모든 executable이 사용 가능 상태로 뜨지 않는다.
- [ ] canonical fuzz corpus에서 crash, hang, sanitizer 오류가 없다.
- [ ] 이후 단계가 사용할 CCJ/digest 공개 계약이 승인되고 동결됐다.

---

## 6. S2 — Action, Tool Schema, Tool Contract, Registry, Config

### S2-01. Core 누락 계약 작성

- [ ] G0-25에 정의한 `action.hpp`, `budget.hpp`, `permission_gate.hpp`, `conversation.hpp`, `ops_log.hpp` 계약을 헤더로 옮긴다.
- [ ] 모든 구조체 field에 필수/선택, 기본값, byte/count 상한, 소유권, lifetime을 주석으로 적는다.
- [ ] `ActionRequest`의 session/turn/action/tool/arguments/ordinal을 정의하고 MVP ordinal은 0만 허용한다.
- [ ] provider가 만든 ID를 그대로 신뢰하지 않고 Core가 생성·검증하는 경계를 고정한다.
- [ ] `Limits`, `Deadline`, `CancelToken`, `GateInput`의 wall/monotonic 사용을 구분한다.
- [ ] `OpsLogger` level과 secret redaction 계약을 정의한다.
- [ ] 공개 구조체와 감사 JSON projection을 `docs/contracts/core-v1.md`에서 일대일 대응시킨다.

### S2-02. Schema dialect 화이트리스트

- [ ] schema tree 전체를 재귀 순회하는 keyword classifier를 구현한다.
- [ ] Tier-G에 object/array/string/integer/boolean, properties, required, `additionalProperties:false`, scalar enum, string length, integer bounds, 제한 pattern을 구현한다.
- [ ] Tier-V에 number, number bounds, exclusive bounds, const, min/maxItems를 구현한다.
- [ ] 원본 §3-2의 금지 keyword 전부를 parameterized 목록으로 만들고 발견 시 compile 실패시킨다.
- [ ] 목록에 없는 keyword도 조용히 무시하지 않고 compile 실패시킨다.
- [ ] `$ref`와 외부 URI loader를 validator 구성 수준에서도 비활성화한다.
- [ ] `$schema`는 Draft-7만 허용하고 누락 시 적용할 dialect를 계약대로 고정한다.
- [ ] nested properties/items 안의 금지 keyword도 빠짐없이 탐지한다.
- [ ] 검사 순서를 keyword whitelist → pattern 분석 → validator compile → coverage 산출로 고정한다.

### S2-03. Pattern parser와 실행 제한

- [ ] G0-10에서 승인된 engine/격리 방식을 의존성 및 빌드에 반영한다.
- [ ] 시작 `^`, 끝 `$`, UTF-8 256 bytes 상한을 검사한다.
- [ ] `{n,m}`의 문법과 `m<=1024`, `n<=m`을 검사한다.
- [ ] nested quantifier, backreference, lookaround를 token/parser 기반으로 거부한다.
- [ ] `*`/`+`가 문자 class 또는 단일 literal 뒤에만 오는지 검사한다.
- [ ] escape, class의 `]`/`-`/`^`, group 중첩, malformed pattern을 경계 테스트한다.
- [ ] runtime 200ms를 넘기기 전에 안전하게 종료하고 `pattern_timeout`을 반환한다.
- [ ] timeout worker/thread/process가 남지 않고 다음 요청의 자원을 점유하지 않는지 확인한다.
- [ ] timeout 세부정보는 OpsLogger ERROR에 남기되 원문 민감 argument를 기록하지 않는다.

### S2-04. CompiledSchema와 grammar coverage

- [ ] 입력 schema와 G0-27의 output schema를 모두 immutable `CompiledSchema`로 만든다.
- [ ] validator가 던지는 예외를 `SchemaCompileFailed` 또는 `SchemaViolation`으로 변환한다.
- [ ] 전체 subtree의 keyword를 기준으로 `Full`, `Partial`, `None`을 계산한다.
- [ ] `type/properties/required`만 있는 schema를 `None`으로 분류한다.
- [ ] Tier-V keyword 목록을 정렬·중복 제거해 감사용 projection에 넣는다.
- [ ] check 실패 detail에 bounded JSON pointer를 넣고 전체 arguments를 복사하지 않는다.
- [ ] `Full/Partial/None` 대표, 혼합 중첩, number bounds, constraint 없는 object를 테스트한다.
- [ ] compile 결과와 GBNF generator가 서로 다른 coverage를 보고하면 startup 실패하게 한다.

### S2-05. ToolDescriptor와 effect/risk 하한

- [ ] `Effect`, `Risk`, `Idempotency`, `ToolStatus`, `ToolResultStatus`의 안정 문자열을 정의한다.
- [ ] tool name 형식/128 bytes 상한, timeout 양수, output byte 상한, provider/invoker ID를 검사한다.
- [ ] enabled tool은 handler와 input schema가 필수다.
- [ ] forbidden tool은 `forbidden_reason`이 필수이며 handler 실행이 불가능해야 한다.
- [ ] `effect:none → risk>=low`, `write → risk>=medium`, `destructive → risk=high` 하한을 검사한다.
- [ ] destructive는 `approval_required=true`, `idempotency=unsafe`를 강제한다.
- [ ] write의 기본 approval/idempotency와 명시적 면제 감사 규칙을 적용한다.
- [ ] invalid descriptor를 자동 보정하지 않고 `ToolContractViolation`으로 startup 실패시킨다.
- [ ] effect × risk × approval × idempotency 전체 행렬을 table-driven test로 만든다.

### S2-06. Registry 등록, tombstone, Freeze

- [ ] `std::map` 또는 동등한 이름순 권위 저장소를 사용한다.
- [ ] duplicate name을 원자적으로 거부한다.
- [ ] `RegisterFrom(provider)`는 전체 성공 또는 전체 rollback이다.
- [ ] forbidden 도구는 삭제하지 않고 tombstone으로 보존한다.
- [ ] Lookup이 `Enabled`, `Forbidden`, `Absent`를 정확히 구분한다.
- [ ] Freeze 시 모든 input/output schema를 compile하고 Tool contract를 검증한다.
- [ ] Freeze 중 하나라도 실패하면 partial frozen/partial compiled 상태가 남지 않는다.
- [ ] Freeze 후 register, tombstone 변경, provider deny 추가를 거부한다.
- [ ] model export는 handler와 secret을 제외하고 이름순으로 만든다.
- [ ] schema export order version을 `name-asc-v1`로 노출한다.

### S2-07. Registry/Policy projection digest

- [ ] G0-26의 field projection 표를 코드 상수 또는 단일 serializer로 구현한다.
- [ ] registry digest에 name, status, forbidden reason, schema, coverage, effect, risk, approval, idempotency, timeout, output limit, provider/invoker identity 중 승인된 필드를 넣는다.
- [ ] handler 주소, object address, map insertion order는 digest에서 제외한다.
- [ ] policy digest에 schema version과 정규화된 전체 rule 권위 필드를 넣는다.
- [ ] 누락 optional field는 생략/`null`/빈 문자열 중 한 표현만 사용한다.
- [ ] enum은 숫자가 아니라 고정 lowercase 문자열 projection을 사용한다.
- [ ] 등록/파일 순서가 달라도 동일한 digest가 나오는지 확인한다.
- [ ] 각 권위 field 하나를 바꾸면 digest가 달라지는 mutation test를 만든다.
- [ ] 3 compiler에서 registry/policy digest 벡터를 비교한다.

### S2-08. Config Schema와 SecretString

- [ ] `config/cogito.schema.json`, `policy.schema.json`, `tool-manifest.schema.json`, `models.schema.json`의 version과 unknown-field 정책을 고정한다.
- [ ] 모든 config를 Strict JSON으로 읽고 schema compile/check 후에만 객체를 만든다.
- [ ] config에는 secret 값 대신 `env:`, `file:`, `wincred:`, `keyring:` 참조만 허용한다.
- [ ] `SecretString`은 copy 금지, move 허용, 소멸 시 best-effort zeroization을 수행한다.
- [ ] `Expose()` 호출 범위를 실제 HTTP/TLS 설정 직전으로 제한한다.
- [ ] Unix secret file owner/mode와 Windows ACL 검사를 구현한다.
- [ ] world/group readable 파일, symlink/reparse point 우회, 지나치게 큰 secret을 거부한다.
- [ ] config/audit/OpsLogger/Error/UI에 secret canary가 남지 않는 통합 테스트를 만든다.
- [ ] config digest에는 secret 원문이 아니라 승인된 reference metadata만 넣는다.
- [ ] schema migration/version mismatch는 자동 추측하지 않고 명확히 실패시킨다.

### S2-09. S2 테스트와 startup 실패 경로

- [ ] `tests/core/schema`에 모든 허용/Tier-V/금지/unknown keyword를 넣는다.
- [ ] `tests/core/registry`에 duplicate, rollback, tombstone, freeze, export, digest를 넣는다.
- [ ] `tests/core/tool_contract`에 effect/risk 행렬과 invalid descriptor를 넣는다.
- [ ] `tests/core/config`에 malformed/duplicate/secret permission/schema version을 넣는다.
- [ ] `fuzz_schema`에 nested dialect, malicious pattern, huge enum/property corpus를 넣는다.
- [ ] startup pipeline에서 config → secrets → providers → Registry Freeze → digest 순서를 검증한다.
- [ ] 어떤 startup 실패도 listener, CLI prompt, AgentLoop ready 상태에 도달하지 않게 한다.

**S2 Exit Gate**

- [ ] unknown/forbidden schema keyword는 예외 없이 startup 실패한다.
- [ ] `grammar_coverage`, registry export, registry/policy digest가 결정적이다.
- [ ] invalid effect/risk 및 forbidden/absent 도구는 실행 경로가 없다.
- [ ] secret canary가 생성 artifact와 로그에서 0건이다.
- [ ] output schema 검증 계약이 Invoker 구현 전 동결됐다.

---

## 7. S3 — FSM

### S3-01. 상태, 이벤트, 단일 전이표

- [ ] 원본의 10개 State와 19개 Event를 안정 enum/string으로 구현한다.
- [ ] 19개 명시 전이를 `constexpr` 단일 source of truth로 둔다.
- [ ] 별도 switch/문서용 배열에 전이를 중복 작성하지 않는다.
- [ ] `Done`, `Failed`, `Cancelled`만 terminal로 분류한다.
- [ ] enum 추가·삭제가 DumpTable과 전체 table test를 자동으로 깨뜨리게 한다.

### S3-02. Dispatch와 R1/R2/R3

- [ ] 명시 전이를 먼저 찾고 그 뒤 universal rule을 적용한다.
- [ ] G0-24에서 확정한 상태에만 `AuditError → Failed`를 적용한다.
- [ ] 허용 nonterminal 상태의 `Cancel → Cancelled`를 적용한다.
- [ ] Execute에서 Cancel event를 거부하고 CancelToken/ToolResult 경로만 사용한다.
- [ ] undefined transition은 즉시 상태를 Failed로 바꾸고 오류와 TransitionRecord를 함께 만든다.
- [ ] record의 from/to/event/cause/action/turn/wall/monotonic을 모두 채운다.
- [ ] 시간은 주입된 Clock에서만 읽는다.
- [ ] Dispatch의 `[[nodiscard]]` 경고를 warning-as-error compile probe로 확인한다.

### S3-03. VerifyTableIntegrity

- [ ] `(from,event)` duplicate를 전수 검사한다.
- [ ] 명시표에 AuditError/Cancel이 들어가 universal rule과 충돌하는지 검사한다.
- [ ] Idle부터 명시+universal edge로 모든 비terminal 상태 도달성을 BFS 검사한다.
- [ ] terminal에서 StartNextTurn 경로를 포함해 다음 턴 도달성을 검사한다.
- [ ] unreachable/duplicate/conflict fixture를 각각 startup 실패시키는 test hook을 만든다.
- [ ] 검사 실패 detail에 정확한 state/event를 넣는다.

### S3-04. DumpTable과 문서 권위

- [ ] DumpTable에 19개 명시 전이와 R1/R2의 실제 상태별 전이를 모두 포함한다.
- [ ] state/event 문자열 및 배열 정렬을 고정한다.
- [ ] CCJ 직렬화된 결과를 `cogito-cli --dump-transitions`가 그대로 출력하게 한다.
- [ ] 출력 digest를 회귀 테스트에 고정한다.
- [ ] 승인된 결과를 ADR 0001에 첨부한다.
- [ ] Web FSM 화면은 후속 단계에서 이 출력만 사용하도록 계약을 명시한다.

### S3-05. 연속 턴

- [ ] 정상 경로에서 `ResetForNextTurn()` 직접 대입을 제거하거나 생성 전용 private helper로 제한한다.
- [ ] Done/Failed/Cancelled에서 `Dispatch(StartNextTurn)`을 거쳐 Idle로 간다.
- [ ] StartNextTurn transition도 다른 전이와 같은 방식으로 감사될 수 있게 한다.
- [ ] 정상→정상, 실패→정상, 취소→정상 2턴 시퀀스를 테스트한다.
- [ ] 두 번째 턴에 이전 action/approval/gate reentry 상태가 잘못 남지 않는지 검사한다.

**S3 Exit Gate**

- [ ] 19개 명시 전이와 모든 R1/R2 적용 상태가 table-driven test를 통과한다.
- [ ] Execute/Cancel, undefined transition, terminal event 경계가 고정됐다.
- [ ] VerifyTableIntegrity가 startup에 연결됐다.
- [ ] DumpTable과 실제 Dispatch 결과가 동일하다.
- [ ] 연속 2턴 이상이 transition 누락 없이 통과한다.

---

## 8. S4 — Identity, Policy, Budget, Approval, Permit

### S4-01. Subject와 ExecutionMode

- [ ] Subject의 안정 `subject_id`, roles, auth_method, line_id 규칙과 byte/count 상한을 정의한다.
- [ ] role을 정규화·중복 제거하되 인증원이 부여한 의미를 변경하지 않는다.
- [ ] 빈/잘못된 subject를 fail-closed 한다.
- [ ] G0-29의 4 mode × 3 effect 허용 행렬을 구현한다.
- [ ] 요청 mode가 config/role이 허용한 상한을 높이지 못하게 한다.
- [ ] enum ordinal 비교로 권한을 계산하는 코드가 없는지 정적 검색한다.
- [ ] mode × role × effect/risk 전체 조합을 test fixture로 만든다.

### S4-02. Policy 파싱과 matching

- [ ] unique rule ID, integer priority, known enum/range, line scope를 schema로 검사한다.
- [ ] tool pattern은 exact, `*`, 점 구분 prefix glob만 허용하고 임의 regex를 허용하지 않는다.
- [ ] tool/mode/role/effect/risk/line 조건을 모두 적용한다.
- [ ] highest priority matching rule 전체를 모은다.
- [ ] 같은 priority 충돌은 `Deny > Ask > Allow`로 선택하고 conflict flag를 남긴다.
- [ ] matching rule 없음은 Deny다.
- [ ] file order가 판정/rule ID/digest를 바꾸지 않게 한다.
- [ ] `AddHighPriorityDenies`는 Freeze 이전에만 허용하고 policy digest를 재계산한다.
- [ ] provider deny가 일반 allow보다 항상 우선하는지 테스트한다.

### S4-03. Budget inference 예약과 정산

- [ ] prompt/completion/total/tool call/repeat/deadline Limit 타입을 구현한다.
- [ ] inference 호출 전에 예상 prompt와 max completion을 예약한다.
- [ ] 예약 실패 시 provider 호출은 0회다.
- [ ] 실제 Usage 도착 후 예약을 정산하고 초과 사용을 숨기지 않는다.
- [ ] provider error, timeout, cancel의 charge/정산 규칙을 고정한다.
- [ ] 음수 환불, overflow, 이중 정산을 차단한다.
- [ ] exact limit와 limit+1, actual<reserved, actual>reserved를 테스트한다.

### S4-04. Tool call/repeat/deadline budget

- [ ] Gate의 budget check와 실제 실행 시 counter commit 시점을 분리한다.
- [ ] 승인 재진입이 tool call/repeat counter를 이중 소비하지 않게 한다.
- [ ] 같은 action digest 반복 상한을 적용한다.
- [ ] G0-05 operation digest 잠금과 budget repeat 의미를 혼동하지 않는다.
- [ ] deadline은 monotonic이고 승인 대기 중에도 흐른다.
- [ ] wall clock 역행, 승인 만료, exact deadline 경계를 FakeClock으로 테스트한다.
- [ ] `budget_tokens`, `budget_tool_calls`, `budget_repeat`, `budget_deadline`을 정확히 구분한다.

### S4-05. ApprovalRecord와 상태 머신

- [ ] Pending, Approved, Rejected, Expired, Cancelled, Consumed 허용 전이를 표로 구현한다.
- [ ] CreatePending이 UUID, CSPRNG nonce, requester, effect/risk, action/scope/session/turn/action ID, policy/registry digest, monotonic TTL을 저장한다.
- [ ] pending 64개 상한을 넘으면 새 pending을 만들지 않는다.
- [ ] Respond가 approval ID, action digest, nonce, approver Subject를 모두 검증한다.
- [ ] effect가 있는 작업의 자기 승인을 기본 거부하고 예외 정책을 감사한다.
- [ ] G0-30 결정에 따라 만료 mutation 경로를 owner thread 하나로 제한한다.
- [ ] FindUsable이 action/scope/session/turn/current policy/current registry/expiry를 모두 확인한다.
- [ ] Consume은 Approved에서 정확히 한 번만 성공한다.
- [ ] `Pending()`이 내부 pointer lifetime을 호출자가 mutation 후 유지하지 못하게 snapshot/value 형태를 검토한다.
- [ ] 64/65, stale, 변조, 다른 session/turn, 재사용, self approval을 테스트한다.

### S4-06. Gate 재진입 카운터

- [ ] G0-31에서 정한 증가 시점과 reset 시점을 action 상태에 구현한다.
- [ ] 초기 Ask는 pending을 만들고 아직 “재진입”으로 잘못 계산하지 않는다.
- [ ] 승인 후 최초 Resume은 Gate 1부터 다시 평가한다.
- [ ] policy/registry 변경으로 다시 Ask가 필요한 경우 허용/거부 횟수를 규범 예제와 일치시킨다.
- [ ] 상한을 넘는 Ask는 `approval_reentry_exceeded` Deny이고 pending을 추가하지 않는다.
- [ ] 새 action/turn에는 이전 counter가 전파되지 않는다.

### S4-07. ExecutionPermit

- [ ] private 기본 생성자와 PermissionGate/ToolInvoker friend 경계를 유지한다.
- [ ] copy construct/assign을 compile-time 금지한다.
- [ ] move 후 source를 invalid로 만들고 move assignment의 기존 자원을 안전하게 처리한다.
- [ ] action/scope/tool/idempotency/expiry/timeout/effect를 immutable하게 보존한다.
- [ ] consume은 ToolInvoker만 handler 직전에 한 번 수행한다.
- [ ] 만료, tool mismatch, scope mismatch, 재사용을 handler 호출 전에 거부한다.
- [ ] Permit 없이 handler에 도달하는 공개 호출 경로가 컴파일/링크 수준에 없는지 확인한다.

**S4 Exit Gate**

- [ ] Policy가 file order와 무관하며 conflict는 제한적인 판정을 택한다.
- [ ] Budget은 provider/tool 호출 전에 fail하며 재진입에서 이중 차감되지 않는다.
- [ ] nonce/digest/scope/주체/만료/소비 중 하나라도 틀리면 write 0회다.
- [ ] Permit은 move-only·single-use이고 PermissionGate 이외 생성 경로가 없다.
- [ ] 승인 상태표와 재진입 표가 코드/테스트/ADR에서 동일하다.

---

## 9. S5 — PermissionGate와 Audit Journal

### S5-01. GateInput과 순수 Evaluate

- [ ] GateInput에 current Subject pointer/value, mode, FSM state, action/scope digest, now UTC/ns, reentry count, lockdown 상태를 정의한다.
- [ ] Subject/tool pointer가 null인 경우 dereference하지 않고 Deny한다.
- [ ] Evaluate는 audit, pending mutation, Permit 발급, FSM transition, 대기를 수행하지 않는다.
- [ ] Verdict 기본값은 Deny이고 모든 반환 경로가 stage/reason/digests/time/expiry를 채운다.
- [ ] `kVerdictTtlNs` 값과 승인 대기 후 만료 의미를 ADR로 고정한다.

### S5-02. Gate 1~4

- [ ] 1단계에서 required field, tool name, UTF-8, action bytes/depth와 Strict JSON 오류를 검사한다.
- [ ] 2단계에서 tombstone을 Absent보다 먼저 판정한다.
- [ ] allow-all 정책에서도 forbidden/absent를 각각 `tool_forbidden`/`tool_not_registered`로 Deny한다.
- [ ] 3단계에서 input schema와 pattern timeout을 구분하고 GBNF 여부와 상관없이 매번 검사한다.
- [ ] 4단계에서 현재 상태가 정확히 Gate인지 검사한다.
- [ ] 복합 오류 fixture로 앞 단계가 항상 우선하는지 확인한다.
- [ ] 앞 단계 실패 시 뒤 단계 mock 호출 횟수가 0인지 검사한다.

### S5-03. Gate 5~7

- [ ] 5단계에서 mode, Subject/role, line scope, policy를 순서대로 적용한다.
- [ ] policy conflict와 no-match reason을 구분한다.
- [ ] G0-04에서 확정한 indeterminate 강등을 policy Allow보다 강하게 적용하되 policy Deny를 약화하지 않는다.
- [ ] 6단계에서 token/tool/repeat/deadline budget을 검사한다.
- [ ] 7단계에서 descriptor approval, policy Ask, destructive, lockdown을 합산한다.
- [ ] 유효 Approved record가 있으면 정확한 approval ID를 구조화 field로 전달한다.
- [ ] G0-31 상한을 넘는 Ask는 Deny한다.
- [ ] approval_required/destructive를 Allow 정책으로 우회할 수 없는지 테스트한다.
- [ ] 정책/Registry digest 변경 뒤 과거 승인이 무효인지 테스트한다.

### S5-04. Permit 준비

- [ ] IssuePermit은 Gate class 이외에서 호출할 수 없다.
- [ ] IssuePermit 자체는 감사/승인 소비/FSM 전이를 하지 않는다.
- [ ] permit TTL과 tool timeout 관계를 고정하고 overflow를 검사한다.
- [ ] idempotency key는 action digest lowercase hex와 byte-identical이다.
- [ ] ToolDescriptor snapshot 변경 가능성을 Freeze로 제거한다.

### S5-05. AuditEvent와 RecordingAuditJournal

- [ ] AuditEvent의 event/session/turn/action/wall/monotonic/epoch/kind/actor/payload/schema version을 구현한다.
- [ ] event kind와 ActorType의 안정 문자열/version을 정의한다.
- [ ] Recording journal은 event와 payload를 deep copy하고 commit order를 보존한다.
- [ ] N번째 commit, 특정 kind, canonicalization, hash 실패를 주입할 수 있게 한다.
- [ ] journal mock 자체가 thread-safe해야 하는 범위를 명시한다.
- [ ] AgentLoop 테스트가 실제 SQLite 없이 모든 commit 지점 실패를 재현하게 한다.

### S5-06. SQLite DDL과 append-only 방어

- [ ] 원본 §7-4 DDL을 migration 001로 만든다.
- [ ] `seq`를 전역 순서 권위로 사용하고 unique event ID를 적용한다.
- [ ] hash/prev hash는 32-byte BLOB constraint를 둔다.
- [ ] `process_epoch_id`와 monotonic을 별도 column으로 둔다.
- [ ] 모든 connection에 WAL, FULL, foreign_keys를 확인한다.
- [ ] UPDATE/DELETE를 trigger와 SQLite authorizer 두 겹으로 차단한다.
- [ ] application DB user가 trigger/authorizer를 제거하지 못하는 운영 권한을 정의한다.
- [ ] migration 중/실패/재실행/낮은 schema version/높은 schema version을 테스트한다.

### S5-07. Durable Commit과 hash chain

- [ ] prev head read, payload CCJ, hash 계산, INSERT, COMMIT을 하나의 write transaction으로 수행한다.
- [ ] in-memory head는 COMMIT 성공 후에만 갱신한다.
- [ ] single writer를 mutex/owner thread로 보장해 chain branch를 막는다.
- [ ] busy timeout을 무한으로 두지 않고 실패를 호출자에게 전달한다.
- [ ] DB lock, disk full, fsync/commit, CCJ, hash 실패 시 partial row/head가 없는지 확인한다.
- [ ] 민감 field masking 후의 payload만 hash/저장하고 masking 규칙 version을 기록한다.
- [ ] Commit 성공의 durability 의미를 실제 crash test로 검증한다.

### S5-08. VerifyChain

- [ ] seq 오름차순으로 모든 semantic field와 canonical payload를 재해시한다.
- [ ] payload 자체가 CCJ byte 형식인지 확인한다.
- [ ] prev hash, current hash, chain head를 모두 대조한다.
- [ ] field mutation, payload mutation, 중간 행 삭제, 순서 변경, head 변경을 탐지한다.
- [ ] monotonic은 같은 process epoch 안에서만 증가를 검사한다.
- [ ] Verify 실패 시 startup ready를 막고 읽기 전용 진단 외 실행을 금지한다.

### S5-09. RecoverDangling

- [ ] `tool_call_started` 뒤 대응 결과가 없는 action을 정확히 찾는다.
- [ ] 각 dangling action을 성공/실패로 추측하지 않고 indeterminate로 복구한다.
- [ ] `tool_result/indeterminate`와 `audit_recovery` 순서·transaction 규칙을 고정한다.
- [ ] recovery를 두 번 실행해도 duplicate event가 생기지 않게 한다.
- [ ] 복구 결과를 AgentLoop의 operation/line lockdown 초기 상태에 주입한다.
- [ ] crash 시점별 fixture로 started 전/commit 후/handler 중/result 전/turn_end 전을 테스트한다.

### S5-10. Audit 조회 경계 준비

- [ ] write connection과 별도의 read-only/query-only connection factory를 만든다.
- [ ] 동일 authorizer와 field projection allowlist를 적용한다.
- [ ] page/transaction/concurrency 상한을 Core query 계약에 둔다.
- [ ] WAL/디스크 임계에서 조회를 먼저 차단할 hook을 만든다.
- [ ] audit read가 writer를 장시간 차단하지 않는 부하 test를 준비한다.

**S5 Exit Gate**

- [ ] Gate 1→7 순서와 reason code가 전체 행렬에서 고정됐다.
- [ ] Gate는 순수하며 audit/Permit/상태 변경이 없다.
- [ ] audit chain의 mutation/delete/reorder/head 변조가 모두 탐지된다.
- [ ] audit DB lock/disk/hash 실패를 상위 루프가 write 0회로 처리할 수 있다.
- [ ] dangling recovery가 멱등이고 indeterminate 안전 상태를 만든다.

---

## 10. S6 — Conversation, Inference, Invoker, AgentLoop

### S6-01. ConversationStore와 ContextCompactor

- [ ] Message role/content/action/tool linkage와 `untrusted`, provenance를 보존한다.
- [ ] Tool/RAG/MCP 유래 데이터는 기본 `untrusted=true`다.
- [ ] pending approval, system policy, 최신 tool result, 안전 경고를 compaction에서 제거하지 않는다.
- [ ] compactor는 같은 입력에서 byte-identical 출력을 만든다.
- [ ] compactor version 문자열을 공개하고 골든 replay key에 포함한다.
- [ ] byte/token 상한과 보호 메시지 초과 시 fail/절단 규칙을 정한다.
- [ ] tool result를 자연어 성공 문장으로 바꾸지 않고 구조화 사실을 유지한다.

### S6-02. Inference 계약과 FakeProvider

- [ ] InferenceRequest에 messages/tools/schema export order/model identity/budget/deadline/cancel을 정의한다.
- [ ] 요청 시 single Action을 강제하는 provider flag를 켠다.
- [ ] InferenceResponse는 text 또는 action 0/1개와 Usage/FinishReason/identity를 반환한다.
- [ ] action 2개 이상은 provider contract violation, FSM Failed, tool 호출 0회다.
- [ ] Length/Error finish reason에서 partial action을 실행하지 않는다.
- [ ] FakeProvider는 0/1/multiple action, provider error, length, cancel, usage를 script ID/version으로 재현한다.
- [ ] 예상 외 호출과 script 소진은 test를 즉시 실패시킨다.

### S6-03. FakeTool과 Recording fixtures

- [ ] FakeTool이 call count, canonical input, idempotency key, deadline/cancel을 기록한다.
- [ ] Ok/Error/Timeout/Cancelled/Indeterminate/throw/hang/oversize/invalid output을 재현한다.
- [ ] write operation은 최대 호출 1회를 fixture 자체에서도 assert한다.
- [ ] fixture ID/version을 골든 key에 넣는다.
- [ ] real clock/network 없이 전체 루프 시나리오를 실행한다.

### S6-04. ToolInvoker

- [ ] Permit valid/tool/action scope/expiry/idempotency를 handler 전에 검사한다.
- [ ] handler 호출 직전에 Permit을 consume한다.
- [ ] Registry에서 handler를 추출할 수 있는 유일한 소비자가 Invoker인지 접근 제어로 보장한다.
- [ ] G0-28의 timeout/cancel 격리 전략을 적용한다.
- [ ] handler exception을 ToolResult Error로 변환하고 C ABI까지 전파하지 않는다.
- [ ] result byte 상한 → strict JSON → output schema 순서로 검증한다.
- [ ] invalid/oversize output을 성공으로 기록하지 않는다.
- [ ] Execute 중 cancel은 effect none이면 Cancelled, effect가 있으면 Indeterminate다.
- [ ] write/destructive 자동 재시도는 0회다.
- [ ] started/finished/elapsed/attempt count를 Clock으로 채운다.

### S6-05. AgentLoop startup과 owner guard

- [ ] dependency null, config/secret, CCJ self-test, FSM integrity, audit chain/recovery, Registry Freeze를 모두 통과한 뒤 ready가 된다.
- [ ] AgentLoop owner thread ID를 기록한다.
- [ ] `in_call_` 또는 동등 guard로 재진입/동시 호출을 거부한다.
- [ ] sealed/finalize_pending/active turn 상태별 허용 API를 표로 구현한다.
- [ ] recovery된 indeterminate 잠금을 turn 시작 전에 복원한다.
- [ ] destructor가 active/finalize-pending 상태를 조용히 버리지 않도록 G0/ABI 정책을 적용한다.

### S6-06. 턴 시작과 inference

- [ ] terminal 상태면 `StartNextTurn`을 Dispatch/감사한 뒤 UserInput을 처리한다.
- [ ] user input을 strict UTF-8/byte 상한으로 검증한다.
- [ ] requester Subject/auth method/roles/line, config/policy/registry/model/template/asset digest, coverage 요약을 `turn_begin` projection에 넣는다.
- [ ] turn_begin commit 실패 시 provider/tool 호출 0회다.
- [ ] Budget 예약 뒤 inference_requested를 기록하고 provider를 호출한다.
- [ ] no action, one action, multiple action, provider error, budget exhaustion을 FSM event에 정확히 매핑한다.
- [ ] 모든 조기 return을 Finalize 또는 PendingApproval 하나로 수렴시킨다.

### S6-07. Gate commit protocol — Deny/Ask

- [ ] 모든 Verdict를 상태 전이 전에 감사한다.
- [ ] verdict commit 실패는 AuditError→Failed이며 뒤 단계 호출 0회다.
- [ ] Deny는 commit → Fire(Deny) → bounded 구조화 denial message 순이다.
- [ ] Ask는 pending 생성 → approval_requested commit → Fire(Ask) → PendingApproval outcome 순이다.
- [ ] approval_requested commit 실패 시 외부에 usable pending이 남지 않도록 cancel/격리 보상 규칙을 구현한다.
- [ ] PendingApproval outcome에 approval/action ID, tool, effect/risk/coverage, CCJ args, reason, digest, nonce, expiry, requester를 넣는다.
- [ ] Pending 상태에서는 turn_end를 기록하지 않고 active turn snapshot을 보존한다.

### S6-08. Gate commit protocol — Allow/Invoke

- [ ] `tool_call_started + idempotency_key` commit이 성공하기 전 Permit을 만들거나 handler를 호출하지 않는다.
- [ ] 감사 성공 뒤 승인 경유 시 정확한 approval을 Consume한다.
- [ ] Consume 실패 시 handler 0회이며 started event의 보상/복구 의미를 ADR과 일치시킨다.
- [ ] Permit 발급 → Fire(Allow) → Invoke 순서를 유지한다.
- [ ] ToolResult/Indeterminate 감사 commit 뒤에만 Execute→Observe 전이를 수행한다.
- [ ] 결과 감사 실패는 AuditError→Failed이고 side effect가 이미 있었을 수 있음을 운영 상태에 보존한다.
- [ ] 각 commit 위치 N번째 실패 주입으로 handler count와 FSM/audit sequence를 검사한다.

### S6-09. 비동기 승인과 ResumeTurn

- [ ] pending action/requester/turn/budget/reentry snapshot을 deep copy해 보존한다.
- [ ] 승인자와 원 requester를 분리해 감사한다.
- [ ] Approved만 Fire(Approved) 후 Gate 1부터 전부 재평가한다.
- [ ] Rejected/Expired는 RejectedOrExpired→Observe로 간다.
- [ ] Pending 상태 Resume의 오류와 상태 불변을 정의한다.
- [ ] policy/Registry/mode/budget/expiry/scope가 바뀌면 과거 Allow를 재사용하지 않는다.
- [ ] 승인 사용은 Permit 발급 경로에서 한 번만 Consume한다.
- [ ] approve→resume write 1회, reject/expire/cancel/변조/reuse write 0회를 검증한다.

### S6-10. Observe와 indeterminate

- [ ] ToolResult를 `untrusted` 구조화 message로 추가한다.
- [ ] indeterminate에는 before/requested/after:null/note만 넣고 성공·실패를 서술하지 않는다.
- [ ] G0-05 operation digest로 같은 tool+canonical args를 session 동안 잠근다.
- [ ] line write lockdown을 모든 관련 write GateInput에 전달한다.
- [ ] outcome의 `had_indeterminate`를 true로 유지한다.
- [ ] 자동 재시도 또는 “확인되지 않은 성공” 변환이 없음을 검사한다.
- [ ] Acknowledge는 step-up된 operator Subject와 note를 받아 `operator_ack` 감사 성공 후에만 잠금을 해제한다.
- [ ] ack 감사 실패, 잘못된 역할, 빈 note에서는 잠금을 유지한다.
- [ ] 같은 operation/다른 action ID, 같은 line의 다른 write, 다른 line을 각각 테스트한다.

### S6-11. Cancel

- [ ] Execute 밖의 취소는 R2를 통해 Cancelled로 간다.
- [ ] Execute 중에는 atomic CancelToken만 설정하고 handler 결과로 분류한다.
- [ ] read/effect none 취소는 Cancelled, write/destructive 취소는 Indeterminate다.
- [ ] Observe에서 여전히 cancel이 설정되면 R2를 적용한다.
- [ ] HTTP/CLI 연결 종료를 자동 cancel로 해석하지 않는다.
- [ ] cancel requester, command ID, 시점, 실제 관찰 지점을 감사한다.
- [ ] blocking/hung handler의 cancel latency가 G0-28 계약 상한을 만족하는지 측정한다.

### S6-12. Finalize, RetryFinalize, SealSession

- [ ] 성공/Deny/오류/취소/timeout 모든 최종 경로가 하나의 Finalize로 온다.
- [ ] 최초 finalize에서 완성한 event ID와 CCJ payload를 immutable snapshot으로 보존한다.
- [ ] turn_end는 final state/status/indeterminate/usage/transition count/verdict count를 포함한다.
- [ ] 성공 시 active/pending 상태를 한 번만 정리한다.
- [ ] 실패 시 finalize_pending=true이고 RunTurn/ResumeTurn을 막는다.
- [ ] RetryFinalize는 원래 event를 byte-identical하게 재커밋한다.
- [ ] DB에는 이미 commit됐지만 응답을 못 받은 경우 같은 event ID+payload를 성공으로 인정하고 다른 payload는 오류로 처리한다.
- [ ] SealSession은 audit_recovery commit 성공 뒤에만 sealed 상태가 된다.
- [ ] seal commit도 불가능한 경우 승인된 프로세스 중단 정책과 종료 code를 검증한다.
- [ ] active/finalize_pending/sealed의 불가능 조합을 invariant test로 검사한다.

### S6-13. 골든 replay

- [ ] key에 user input, FakeProvider/Tool ID·version, FakeClock sequence를 넣는다.
- [ ] config/policy/registry digest, compactor version, chat template digest, schema export order version, event queue order를 넣는다.
- [ ] expected transition, full Verdict tuple, tool/action/idempotency, audit kind sequence를 비교한다.
- [ ] 정상 no-action/read/approval write/Deny/provider error/multiple action/audit failure/indeterminate/ack/finalize retry/연속 턴 fixture를 만든다.
- [ ] key 하나 또는 event 순서 하나를 바꾸면 반드시 실패하는 negative test를 둔다.
- [ ] golden 갱신은 전용 명령과 리뷰 승인 없이는 불가능하게 한다.

### S6-14. Core 통합·fuzz·sanitizer

- [ ] `core/fsm`, `gate`, `policy`, `budget`, `approval`, `registry`, `schema` 전체를 실행한다.
- [ ] `audit/chain`, `tamper`, `authorizer`, `recovery`, `failure`, `epoch` 전체를 실행한다.
- [ ] malformed action/schema/policy/CCJ/prompt injection을 fuzz한다.
- [ ] ASan/UBSan에서 Core와 SQLite 경로를 실행한다.
- [ ] TSan에서 owner guard, cancel flag, audit snapshot/read path를 실행한다.
- [ ] 모든 실패 경로에서 handler 호출 count와 turn_end/finalize state를 assert한다.

**S6 Exit Gate**

- [ ] 정상·오류·취소·Deny·승인·만료·indeterminate 경로의 handler 횟수와 감사 순서가 검증됐다.
- [ ] 사전 감사 실패 시 write/destructive handler 호출 0회다.
- [ ] write/destructive 자동 재시도 0회다.
- [ ] PendingApproval 외 모든 턴은 turn_end 1개 또는 명시적 finalize lock/sealed 상태다.
- [ ] 골든 replay가 3 compiler 계열에서 일치한다.
- [ ] Core 관련 sanitizer/fuzz 회귀가 0건이다.
- [ ] 임시 Allow fallback, unresolved TODO, `REPLACE_WITH_*`가 없다.

---

## 11. S7 — C ABI v1.1, CLI, 설치 패키지

### S7-01. ABI v1.1 공개 표면 동결

- [ ] G0-01/G0-02/G0-03/G0-07/G0-08/G0-32를 모두 해소한다.
- [ ] 최초 구현 ABI를 major 1, minor 1로 고정한다.
- [ ] v1.0 고정 Subject, thread-local last error, nonce 없는 approval 시그니처를 실제 코드에 만들지 않는다.
- [ ] 모든 status code 숫자를 명시적으로 고정하고 `COGITO_ERR_WRONG_THREAD`를 포함한다.
- [ ] 상태 변경 함수, 조회 함수, memory 함수, version 함수의 최종 목록을 표로 만든다.
- [ ] 모든 함수 바로 위에 `@thread: agent-loop-only` 또는 `@thread: any`를 쓴다.
- [ ] `agent_create`, `agent_destroy`, `verify_audit_chain`을 포함해 미분류 선언이 0개다.
- [ ] `agent_destroy`의 wrong-thread/반환형 정책을 G0-08 결정과 일치시킨다.
- [ ] 구조체는 `struct_size`, 필요한 경우 `abi_version`을 첫 field에 둔다.
- [ ] 기존 minor 크기의 구조체를 받아들이고 알 수 없는 trailing field를 무시하는 규칙을 고정한다.

### S7-02. 순수 C 헤더

- [ ] `include/cogito/cogito.h`가 C++ header/type/namespace 없이 C11에서 compile된다.
- [ ] opaque `cogito_agent_t`, fixed-width integer, status/mode/effect/risk 상수를 정의한다.
- [ ] compiler별 enum size 차이를 피하도록 필요 시 `int32_t typedef + constants`를 사용한다.
- [ ] Windows static/shared export와 ELF visibility macro를 구현한다.
- [ ] `extern "C"` guard와 calling convention을 고정한다.
- [ ] pointer nullability, input lifetime, output ownership을 함수별로 주석 처리한다.
- [ ] C와 C++ smoke translation unit을 MSVC/GCC/Clang으로 warning-as-error compile한다.
- [ ] header parser가 `@thread` 없는 선언 하나를 의도적으로 잡는 negative test를 둔다.

검증 예시:

```powershell
cl /nologo /W4 /WX /TC tests\abi\c_header_smoke.c /Iinclude /c
```

```bash
gcc -std=c11 -Wall -Wextra -Werror -Iinclude -c tests/abi/c_header_smoke.c
clang++ -std=c++17 -Wall -Wextra -Werror -Iinclude -c tests/abi/cpp_header_smoke.cpp
```

### S7-03. AgentImpl 생성과 수명

- [ ] create 입력 config/문자열/callback table을 모두 deep copy한다.
- [ ] 부분 생성 실패 시 `*out_agent=nullptr`이고 생성된 secret, DB, provider, callbacks를 역순 정리한다.
- [ ] CCJ/FSM self-test, config/secret, audit Verify/Recover, Registry Freeze가 끝나기 전 handle을 반환하지 않는다.
- [ ] owner thread ID를 create 또는 명시적 attach 시점에 기록한다.
- [ ] Core dependency의 생성/소멸 순서를 문서화하고 역의존 순서로 파괴한다.
- [ ] active/finalize-pending handle destroy 정책을 Finalize/Seal 계약과 일치시킨다.
- [ ] caller가 config memory를 create 직후 해제해도 정상 동작하는지 테스트한다.
- [ ] failure injection 지점마다 ASan/LSan/CRT leak 0인지 확인한다.
- [ ] destroy 후 callback/user_data 접근이 0인지 확인한다.

### S7-04. Error mapping과 예외 경계

- [ ] 모든 Core `Errc`를 C status에 빠짐없이 매핑한다.
- [ ] 모든 C entry point를 동일한 guard wrapper로 감싼다.
- [ ] `bad_alloc`, `std::exception`, unknown exception을 C 경계 전에 차단한다.
- [ ] `ex.what()`은 redacted OpsLogger에만 제한적으로 남기고 C 오류에는 고정 문구/reason code만 준다.
- [ ] last error는 handle-scoped mutex 보호 buffer이며 다른 agent/request와 섞이지 않는다.
- [ ] last error 길이 조회, 작은 buffer, null, 정상 copy를 명확히 처리한다.
- [ ] ABI 오류에 secret, 전체 path, stack, raw payload가 들어가지 않는 canary test를 둔다.
- [ ] callback exception에서도 최종 tool_result/turn 상태가 닫히는지 검사한다.

### S7-05. Buffer ownership

- [ ] 모든 ABI output은 라이브러리 allocator로 할당한다.
- [ ] `cogito_buffer_free`만 해제 수단이며 Windows host CRT free를 금지한다.
- [ ] success와 PendingApproval은 유효 JSON buffer를 반환한다.
- [ ] error는 계약대로 null/길이 0을 반환한다.
- [ ] zero-length, null, double free 방지 범위, 잘못된 pointer 처리 계약을 문서화한다.
- [ ] C/C++/C# 소비자에서 allocate→inspect→free 왕복을 반복한다.
- [ ] ASan/LSan과 Windows CRT/Application Verifier에서 heap mismatch가 없는지 확인한다.

### S7-06. Tool registration/callback bridge

- [ ] G0-32의 v1.1 descriptor가 name, input/output schema, effect/risk, approval, idempotency, timeout/output limit, provider/invoker ID, status/reason을 전달한다.
- [ ] callback `struct_size`와 forward compatibility를 검사한다.
- [ ] 각 invocation에 단조 증가/고유 `call_id`를 준다.
- [ ] 1차 크기 질의 → Core allocation → 2차 채우기 순서를 구현한다.
- [ ] 두 callback 호출의 call ID와 input bytes가 동일한지 확인한다.
- [ ] 1·2차 결과 크기 변화, buffer too small 반복, malformed UTF-8/JSON을 Tool Error로 처리한다.
- [ ] callback exception이 C 경계를 넘지 않고 write 자동 재시도가 없게 한다.
- [ ] callback 등록 후 user_data lifetime과 destroy 순서를 명시한다.
- [ ] Registry Freeze 후 register를 거부한다.

### S7-07. 상태 변경 API

- [ ] run turn, resume turn에 요청별 `cogito_subject_t`와 requested mode를 전달한다.
- [ ] approve/reject가 approval ID, 64자 lowercase action digest, nonce, approver Subject를 받는다.
- [ ] cancel을 `request_cancel(@thread:any)`과 audited `cancel_turn(@thread:owner)`로 분리한다.
- [ ] expire approvals, retry finalize, seal session, acknowledge indeterminate를 최종 계약에 포함한다.
- [ ] accepted auth method, Subject field, role/line/mode 상한을 Core 진입 전에 검사한다.
- [ ] 승인 재평가는 원 requester snapshot으로 하고 approver는 별도 actor로 감사한다.
- [ ] PendingApproval JSON에 tool/effect/risk/coverage/CCJ args/reason/digest/nonce/expiry/requester를 넣는다.
- [ ] 승인 API의 success가 곧 turn/tool success를 뜻하지 않게 한다.
- [ ] pending→approve→resume→write 1회와 reject/expire/cancel/변조/self approval→write 0회를 검증한다.

### S7-08. Thread affinity와 snapshot

- [ ] owner-only 함수 진입 첫 동작으로 thread ID를 비교한다.
- [ ] wrong thread면 `COGITO_ERR_WRONG_THREAD`이며 FSM/Budget/Approval/Audit/last outcome은 byte-identical하게 유지된다.
- [ ] `request_cancel`은 atomic flag 이외 상태를 바꾸지 않는다.
- [ ] 실제 cancel transition/audit는 owner thread가 수행한다.
- [ ] any-thread 조회는 mutex 아래 immutable snapshot을 복사하고 lock 밖에서 JSON 직렬화한다.
- [ ] callback 도중 reentrant ABI 호출을 허용/거부하는 표를 작성하고 test한다.
- [ ] TSan에서 owner call, 조회 경쟁, cancel, event wait를 실행한다.

### S7-09. 조회 API

- [ ] state 조회가 FSM, current turn, finalize/sealed/lockdown, pending count, process epoch, digests, server UTC/monotonic을 반환한다.
- [ ] pending approvals 조회가 권위 승인 field와 nonce/remaining TTL을 반환한다.
- [ ] tools 조회가 이름순 descriptor/status/coverage를 반환한다.
- [ ] budget 조회가 limits/reserved/used/deadline을 snapshot으로 반환한다.
- [ ] transitions 조회가 DumpTable의 명시+R1/R2를 반환한다.
- [ ] audit query가 query-only connection, projection allowlist, page≤200을 강제한다.
- [ ] next events가 audit `seq`, `process_epoch_id`, bounded batch/wait를 사용한다.
- [ ] 모든 조회 buffer를 동일 ownership API로 해제한다.
- [ ] writer 진행 중 snapshot 일관성과 event replay 유실/중복 0을 검사한다.

### S7-10. 공개 심볼과 ABI compatibility

- [ ] Linux version script `src/abi/cogito.map`을 작성한다.
- [ ] Windows export list 또는 macro 결과를 검사한다.
- [ ] expected symbol 목록과 실제 `nm -D`/`dumpbin /exports`를 비교한다.
- [ ] 예상 `cogito_*` 이외 C++ symbol 노출을 막는다.
- [ ] 이전 minor `struct_size`, unknown trailing field, wrong abi version을 테스트한다.
- [ ] ABI layout을 compiler/platform별 dump하고 승인된 baseline과 비교한다.
- [ ] breaking change는 major bump 없이는 merge하지 못하게 한다.

### S7-11. CLI

- [ ] CLI는 내부 C++ API가 아니라 C ABI v1.1만 소비한다.
- [ ] config/policy/tools/audit/session/subject/roles/auth/line/mode 옵션을 구현한다.
- [ ] `--dump-transitions`, `--verify-audit`, `--version`, JSON output을 구현한다.
- [ ] ABI buffer를 RAII wrapper로 해제한다.
- [ ] PendingApproval을 받은 뒤 권위 field와 nonce를 보여주고 콘솔 명령으로 approve/reject한다.
- [ ] Tool callback 안에서 승인 입력을 기다리지 않는다.
- [ ] 승인 후 별도 resume API를 호출한다.
- [ ] Ctrl+C handler는 async-signal-safe 범위에서 cancel request만 전달한다.
- [ ] EOF/reject/Ctrl+C에서 write 0회를 scripted stdin으로 테스트한다.
- [ ] raw completion, full config, secret을 stdout/stderr에 출력하지 않는다.

### S7-12. 설치/export 소비자

- [ ] 모든 optional target을 정의한 뒤 install target list를 만든다.
- [ ] build-tree와 install-tree target을 `cogito::core`, `cogito::cogito` 등 승인 이름으로 통일한다.
- [ ] 활성 feature의 transitive dependency를 `cogitoConfig.cmake`에서 `find_dependency`한다.
- [ ] component를 실제 구현하거나 `check_required_components` 사용을 제거한다.
- [ ] C header/library, C++ headers/core, CLI, schema/sample config, licenses를 allowlist 설치한다.
- [ ] source tree와 vcpkg 내부 경로가 없는 clean prefix에서 C/C++ 소비자를 build/run한다.
- [ ] Debug/Release, static/shared, Windows/Linux 조합을 테스트한다.
- [ ] model, secret, certificate private key, audit DB, log가 package에 없는지 검사한다.

### S7-13. 최소 C# P/Invoke 기반

- [ ] G0-16에서 승인한 최소 범위만 구현한다.
- [ ] Cdecl, exact struct layout, fixed-width integer를 선언한다.
- [ ] Agent와 output buffer에 SafeHandle을 사용한다.
- [ ] callback delegate/user_data lifetime을 고정한다.
- [ ] x64 Windows와 승인된 ARM64/Linux RID 전략을 정한다.
- [ ] create/state/run/pending/approve/reject/resume/free smoke를 `dotnet test`한다.
- [ ] native DLL 탐색과 version mismatch를 명시적 오류로 처리한다.

**S7 Exit Gate**

- [ ] C header가 C11/C++17에서 3 compiler 계열로 compile된다.
- [ ] ownership, async approval, exception, finalize, thread affinity, snapshot ABI test가 통과한다.
- [ ] wrong-thread 호출은 상태 무변경이고 TSan 오류 0이다.
- [ ] CLI 승인 흐름은 비동기이며 승인 전 write 0회다.
- [ ] out-of-tree C/C++ 소비자와 최소 C# smoke가 성공한다.
- [ ] 공개 심볼과 ABI layout이 승인 baseline과 일치한다.

---

## 12. S8 — HTTP/OpenAI 호환 및 llama.cpp Provider

### S8-01. Provider identity와 models schema

- [ ] G0-13의 source/link, model/tokenizer/template digest 결정을 완료한다.
- [ ] provider kind, endpoint/GGUF, expected hashes, llama commit/build ID, context/max token/seed를 models schema에 정의한다.
- [ ] remote/local model identity의 digest projection과 domain tag를 고정한다.
- [ ] secret은 reference만 받고 endpoint와 credential을 분리한다.
- [ ] model/template/tokenizer/config hash 누락·불일치 시 startup 실패한다.
- [ ] 동일 설정/파일이 3 compiler에서 같은 ProviderIdentity를 만드는지 검사한다.

### S8-02. HTTP client contract

- [ ] URL/method/headers/body/TLS/connect/read/overall deadline/CancelToken을 backend-neutral interface로 정의한다.
- [ ] response status/header와 bounded body 또는 bounded SSE callback을 정의한다.
- [ ] redirect 기본 거부, compressed/decompressed bytes 상한을 둔다.
- [ ] secret header redaction과 request/response body 미기록을 강제한다.
- [ ] httplib와 선택 curl backend가 같은 contract test를 통과하게 한다.
- [ ] config가 모순되면 configure/startup에서 실패한다.

### S8-03. cpp-httplib backend

- [ ] TLS 검증을 기본 ON으로 하고 CA/client cert/key reference를 지원한다.
- [ ] URL을 scheme/host/port/path로 정규 파싱한 뒤 endpoint allowlist를 적용한다.
- [ ] userinfo, 후행 점, 대소문자, IPv6, encoded path 우회를 테스트한다.
- [ ] redirect를 비활성화하고 3xx를 명시 오류로 처리한다.
- [ ] connect/read/overall timeout과 cancel을 실제 socket 동작에 연결한다.
- [ ] body/SSE frame/전체 stream 상한을 압축 해제 후에도 적용한다.
- [ ] 로컬 TLS mock으로 wrong CA/hostname/redirect/oversize/slow response를 테스트한다.
- [ ] allowlist 밖 host에는 socket connect 시도 0회인지 계측한다.

### S8-04. 선택 curl backend

- [ ] corporate proxy/mTLS/private CA 요구가 승인된 profile에서만 build한다.
- [ ] redirect를 끄고 write/header callback에 size cap을 둔다.
- [ ] progress callback으로 deadline/cancel을 관찰한다.
- [ ] httplib와 동일한 error classification/contract suite를 통과한다.
- [ ] HTTP feature 없이 curl backend를 켜면 configure 실패한다.

### S8-05. OpenAI 호환 요청

- [ ] Conversation과 이름순 Tool Schema를 결정적 JSON으로 만든다.
- [ ] `parallel_tool_calls=false`를 모든 tool-capable request에 보낸다.
- [ ] temperature/seed/max token/chat template identity를 명시한다.
- [ ] API key를 request header 조립 직전에만 노출한다.
- [ ] 로그에는 bounded metadata와 body digest만 남긴다.
- [ ] mock server에서 request JSON 골든을 byte/semantic 비교한다.

### S8-06. JSON/SSE response parser

- [ ] CRLF, 임의 chunk split, 여러 data line, `[DONE]`을 처리한다.
- [ ] frame 256KiB, 전체 4MiB 또는 승인된 상한을 적용한다.
- [ ] malformed UTF-8/JSON, duplicate key, partial final action을 거부한다.
- [ ] content/tool-call delta를 deterministic하게 조립한다.
- [ ] 최종 Action이 2개 이상이면 ProviderContractViolation + Failed + tool 0회다.
- [ ] 모든 byte 위치에서 chunk를 나누는 parameterized test를 만든다.
- [ ] raw completion은 감사/Ops log에 저장하지 않고 필요한 경우 digest만 남긴다.

### S8-07. HTTP failure/usage/retry 정책

- [ ] Usage와 FinishReason을 Core enum으로 손실 없이 매핑한다.
- [ ] timeout/cancel/TLS/4xx/5xx/malformed/oversize를 안정 Errc로 구분한다.
- [ ] 자동 retry는 ADR에서 명시하지 않는 한 0회다.
- [ ] retry가 승인·idempotency·budget을 우회하지 않게 한다.
- [ ] provider가 single-action flag를 무시해도 실행 0회임을 테스트한다.
- [ ] server-reported model ID를 신뢰 identity로 오인하지 않고 config/expected identity와 함께 감사한다.

### S8-08. llama.cpp 공급망과 build

- [ ] source archive/overlay port/submodule 중 승인 방식을 구현한다.
- [ ] upstream commit SHA와 archive SHA-512, patch digest를 고정한다.
- [ ] configure/build 중 FetchContent나 remote git 접근을 금지한다.
- [ ] CPU/CUDA backend를 제품 profile별 명시적으로 켠다.
- [ ] build options와 commit을 provider identity에 기록한다.
- [ ] `COGITO_WITH_LLAMACPP=OFF`일 때 source/link/dependency가 0인지 확인한다.
- [ ] 폐쇄망에서 source와 cache만으로 재빌드한다.

### S8-09. Tier-G → GBNF

- [ ] Registry의 이름순 export를 단일 입력으로 사용한다.
- [ ] 0개 또는 정확히 1개 tool call만 생성 가능한 top grammar를 만든다.
- [ ] Tier-G keyword만 변환하고 Tier-V를 지원한다고 표기하지 않는다.
- [ ] SchemaCompiler coverage/audit projection과 generator 결과를 대조한다.
- [ ] unsupported keyword가 도달하거나 grammar compile이 실패하면 inference를 시작하지 않는다.
- [ ] unconstrained fallback을 금지한다.
- [ ] runtime schema validation을 항상 유지한다.
- [ ] enum Unicode, escape, signed integer, pattern, nested arrays/objects 골든을 만든다.

### S8-10. llama.cpp inference

- [ ] model/context/sampler/grammar를 RAII로 관리한다.
- [ ] 승인된 chat template로 prompt를 만들고 digest를 기록한다.
- [ ] temperature/seed/max tokens를 적용한다.
- [ ] token loop에서 deadline/cancel을 bounded 간격으로 검사한다.
- [ ] UTF-8 조각을 안전하게 조립하고 최종 text/action을 Strict parse한다.
- [ ] action 수를 parser 뒤 다시 확인한다.
- [ ] EstimateTokens와 실제 inference가 같은 tokenizer를 사용한다.
- [ ] 단일 AgentLoop 소유를 적용하고 병렬 추론을 막는다.
- [ ] 한 byte 변조된 model/template/tokenizer가 startup 실패하는지 확인한다.

### S8-11. MCP 범위 게이트

- [ ] G0-15에서 MVP 제외면 build option, 문서, 광고에 MCP runtime 지원을 표기하지 않는다.
- [ ] 포함이면 누락 §10-3을 보완한 뒤에만 transport를 구현한다.
- [ ] 포함 시 최초 stdio probe의 `server/discover`, modern/legacy negotiation, `-32022` boot 1회 재협상, runtime 재협상 금지를 테스트한다.
- [ ] MCP 결과를 `untrusted` provenance로 표시하고 Tool Registry/Schema/Gate를 우회하지 않게 한다.

**S8 Exit Gate**

- [ ] HTTP mock의 TLS/redirect/size/SSE/multi-action 실패가 전부 fail-closed다.
- [ ] llama grammar 실패는 fallback 없이 중단되고 runtime schema 검증이 유지된다.
- [ ] 모델/template/tokenizer/source identity가 해시로 검증된다.
- [ ] provider cancel/deadline/retry가 Tool write를 추가 발생시키지 않는다.
- [ ] 폐쇄망 provider build에서 숨은 다운로드 0회다.

---

## 13. S9 — OPC UA Adapter

### S9-01. Write semantics ADR와 manifest schema

- [ ] G0-14를 해소하고 일반 `read→compare→write`를 원자 CAS라고 부르지 않는다.
- [ ] 실제 server method/version node가 원자 조건부 write를 제공할 때만 CAS profile로 표기한다.
- [ ] endpoint/security/auth, namespace URI+ExpandedNodeId, operation, exact UA type를 schema에 넣는다.
- [ ] effect/risk/idempotency/approval/timeout/output limit/engineering bound를 넣는다.
- [ ] absolute/relative read-back tolerance, precondition, forbidden/reason을 넣는다.
- [ ] write 전/전송 후/응답 후 disconnect의 Error/Cancelled/Indeterminate 분류표를 작성한다.
- [ ] server offline startup 처리(전체 실패/tombstone)를 profile별로 확정한다.
- [ ] Strict JSON, unknown field, duplicate key, version을 검사하고 manifest digest를 감사한다.

### S9-02. open62541 격리 target

- [ ] `COGITO_WITH_OPCUA`일 때만 dependency와 `cogito_adapter_opcua` shared target을 만든다.
- [ ] open62541 source를 `cogito_core`에 섞지 않는다.
- [ ] OFF build의 dependency tree에 open62541이 0인지 확인한다.
- [ ] 실제 vcpkg exported target과 `openssl` feature를 baseline에서 확인한다.
- [ ] MPL-2.0 notice와 수정 파일 목록을 유지한다.
- [ ] install/export/component와 SBOM에서 optional 경계를 보존한다.

### S9-03. RAII secure client

- [ ] UA_Client/config/Variant/NodeId/String의 소유권 wrapper를 만든다.
- [ ] endpoint allowlist, security policy/mode, server trust, client cert/key를 검사한다.
- [ ] insecure downgrade/fallback을 금지한다.
- [ ] connect/service/overall timeout과 CancelToken을 지원한다.
- [ ] reconnect가 write retry를 일으키지 않게 한다.
- [ ] UA client를 owner thread에서만 사용한다.
- [ ] untrusted cert, wrong endpoint, security downgrade, timeout에서 연결/도구 실행을 막는다.
- [ ] ASan/Valgrind 또는 동등 도구에서 UA object 누수 0을 확인한다.

### S9-04. Manifest ToolProvider

- [ ] 각 entry를 완전한 ToolDescriptor와 handler로 변환한다.
- [ ] tool name/NodeId duplicate를 거부한다.
- [ ] effect/risk/approval 하한을 재검증한다.
- [ ] provider/invoker ID와 manifest digest를 descriptor/audit에 넣는다.
- [ ] 가능하면 startup에서 server DataType을 검증하고 실패 정책을 ADR대로 적용한다.
- [ ] forbidden entry는 삭제하지 않고 Registry tombstone으로 등록한다.
- [ ] forbidden node/tool pattern의 high-priority Deny를 Freeze 전에 주입하고 policy digest를 재계산한다.

### S9-05. Exact UA type codec와 read

- [ ] 지원 scalar type 목록(Boolean, 명시 integer 폭, Float, Double, String 등)을 allowlist로 둔다.
- [ ] signed/unsigned, width, scalar/array mismatch를 편의 cast하지 않는다.
- [ ] namespace index를 고정하지 않고 namespace URI를 해석한다.
- [ ] read result에 status code, source/server timestamp, provenance를 넣는다.
- [ ] read result는 `untrusted=true`다.
- [ ] read tool은 effect none이고 write service 호출 0회임을 mock으로 확인한다.

### S9-06. Write protocol

- [ ] 현재 value/type read를 수행한다.
- [ ] precondition 또는 지원되는 원자 profile을 검증한다.
- [ ] write 직전 cancel/deadline을 다시 확인한다.
- [ ] 정확히 1회 write request를 보낸다.
- [ ] 값을 재read하고 승인된 tolerance로 비교한다.
- [ ] `before`, `requested`, `after`, verification status를 구조화한다.
- [ ] write 전 cancel/precondition 실패는 write 0회인 Error/Cancelled다.
- [ ] 전송 뒤 응답 불명, disconnect, read-back 실패/mismatch는 Indeterminate다.
- [ ] read-back verified일 때만 Success다.
- [ ] write/destructive retry와 type cast는 0회다.

### S9-07. Fault-injection integration

- [ ] in-process open62541 test server를 결정적 fixture로 만든다.
- [ ] 정상 read/write와 승인 전 write 0회, 승인 후 write 1회를 테스트한다.
- [ ] precondition conflict, type mismatch, Bad status를 테스트한다.
- [ ] write 직후 disconnect, read-back mismatch/timeout을 테스트한다.
- [ ] cancel 전/중/후와 forbidden node를 테스트한다.
- [ ] policy/Registry change 후 재승인을 테스트한다.
- [ ] 각 시나리오의 audit/lockdown/TurnOutcome을 골든 비교한다.

### S9-08. C# HMI 시연

- [ ] S7 SafeHandle binding을 사용하고 임의 native call을 추가하지 않는다.
- [ ] pending 권위 field, nonce, digest, requester/approver를 표시한다.
- [ ] approve/reject와 resume을 분리한다.
- [ ] fault-injection server를 사용해 승인 전 write count=0을 시연한다.
- [ ] 승인 후 write count=1, digest/nonce 변조/self approval에서 0을 자동 test한다.
- [ ] x64/ARM64 native asset 선택을 RID별로 검증한다.

**S9 Exit Gate**

- [ ] 어떤 write failure/cancel/disconnect도 규칙에 따라 기계적으로 분류된다.
- [ ] 모든 write 경로에서 실제 request 횟수는 0 또는 1이다.
- [ ] read-back 확인 없는 성공은 0건이다.
- [ ] forbidden node는 allow-all 정책에서도 write 0회다.
- [ ] C# HMI와 CI test server에서 승인 전 write 0/승인 후 정확히 1을 입증했다.

---

## 14. S10 — Web Host와 React Dashboard

### S10-00. 착수 금지 조건 확인

- [ ] S7 C ABI v1.1 Exit Gate가 완료됐다.
- [ ] G0-17~G0-20과 G0-33이 모두 해소됐다.
- [ ] `docs/adr/0009-web-trust-boundary.md`가 Accepted다.
- [ ] `docs/web-threat-model.md`가 보안·Core·현장 운영 검토를 통과했다.
- [ ] 인증원/step-up/승인 역할/SOD 예외/OT-IT 범위/브라우저/감사 field/보존 기간이 확정됐다.
- [ ] 로그인/bootstrap/session cookie/CSRF lifecycle을 `docs/web-auth-profile.md`에 고정했다.
- [ ] 7개 POST·8개 GET의 schema/status/error envelope를 OpenAPI 또는 동등 계약으로 고정했다.
- [ ] command cache와 turn outcome의 TTL/퇴거/재시작 의미가 확정됐다.
- [ ] cancel sequence와 `assets_digest` 주입 ABI가 확정됐다.
- [ ] `web/auth`, `web/csrf`, `web/audit_readpath` test skeleton을 UI보다 먼저 만들었다.

### S10-01. W1~W22 추적성

- [ ] W1: cpp-httplib만 사용하고 Crow/uWebSockets/Beast를 의존성·source에 추가하지 않는다.
- [ ] W2: Core 상태 변경은 AgentLoop owner thread만 수행한다.
- [ ] W3: 모든 명령은 bounded priority FIFO를 통한다. 승인된 cancel atomic flag 예외만 별도 표기한다.
- [ ] W4: 요청별 Subject를 받는 ABI v1.1만 사용한다.
- [ ] W5: Subject는 인증 session에서만 유도하고 body/query/arbitrary identity header를 신뢰하지 않는다.
- [ ] W6: WebSocket server/upgrade/양방향 SSE command path가 없다.
- [ ] W7: process 1개 = line/session/agent 1개이며 다중 운영자는 FIFO로 직렬화된다.
- [ ] W8: loopback 기본, 비loopback은 TLS+auth가 모두 없으면 startup 실패한다.
- [ ] W9: insecure no-auth에서 effect가 있는 모든 tool을 예외 없이 Deny한다.
- [ ] W10: write POST는 auth+Origin+Fetch Metadata+CSRF+JSON을 모두 통과한다.
- [ ] W11: approve/reject/seal/ack/finalize retry에 step-up을 요구한다.
- [ ] W12: effect가 있는 요청의 requester=approver를 기본 거부한다.
- [ ] W13: approve는 202만 반환하고 turn 결과를 기다리지 않는다.
- [ ] W14: HTTP/SSE 연결 종료를 turn cancel로 해석하지 않는다.
- [ ] W15: owner thread가 유휴 시 승인 만료 tick을 수행한다.
- [ ] W16: cancel/finalize retry/seal/indeterminate ack 복구 경로를 제공한다.
- [ ] W17: audit 조회는 C ABI/Core query만 쓰고 host가 DB를 직접 열지 않는다.
- [ ] W18: 승인 권위 영역에는 Core 산출 field만 들어간다.
- [ ] W19: raw HTML/위험 React API의 부재를 lint로 강제한다.
- [ ] W20: 앱이 CSP 등 보안 header를 직접 발급한다.
- [ ] W21: dist를 binary에 embed하고 assets digest를 state/turn_begin에 기록한다.
- [ ] W22: Jetson/target에서 Node/npm/Tailwind build를 실행하지 않는다.
- [ ] 각 W 항목을 구현 file, test, threat-model control에 연결한다.

### S10-02. Web 위협 모델

- [ ] Permit, approval nonce/digest, auth session, step-up, audit DB/WAL, lockdown, TLS key, CSRF, assets, idempotency cache를 자산으로 분류한다.
- [ ] unauthenticated attacker, low-role user, malicious site, proxy/header injection, prompt injection, slow client, local admin, supply-chain attacker를 포함한다.
- [ ] browser→HTTP worker→queue→Agent thread→ABI/Core→audit/PLC 경계를 그린다.
- [ ] trust boundary마다 authentication, authorization, validation, rate/resource limit, audit control을 연결한다.
- [ ] CSRF, clickjacking, UI deception, second command path, wrong thread, audit read DoS, offline supply chain을 모두 다룬다.
- [ ] 각 threat를 code control, test, accepted residual risk 중 하나에 연결한다.
- [ ] residual risk에 owner와 review date를 둔다.

### S10-03. CMake와 dependency 경계

- [ ] `COGITO_WITH_WEB`과 `COGITO_WEB_REBUILD`를 기본 OFF로 정의한다.
- [ ] Web은 이미 고정한 httplib/OpenSSL/audit target을 재사용한다.
- [ ] Node가 없는 환경에서도 committed embedded asset으로 C++ build한다.
- [ ] `COGITO_WEB_REBUILD=ON`일 때만 build 단계 custom command가 npm을 실행한다.
- [ ] configure 단계에는 npm/npx/network 호출이 없다.
- [ ] `npx shadcn add`를 build path에서 금지하고 필요한 `.tsx` source를 commit한다.
- [ ] OFF build의 symbol/dependency/package에 Web code가 0인지 확인한다.
- [ ] vcpkg/C++ dependency graph가 Web 때문에 승인되지 않은 server library를 추가하지 않았는지 검사한다.

### S10-04. HTTP config와 startup checks

- [ ] bind address/port/TLS/auth/session/step-up/origin/CSRF/approval/limits를 config schema에 추가한다.
- [ ] unknown field, invalid range, duplicate origin을 거부한다.
- [ ] 비loopback이고 TLS 또는 auth가 없으면 listener open 전에 실패한다.
- [ ] insecure mode+비loopback을 실패시킨다.
- [ ] insecure mode에서는 high-priority deny를 Freeze 전에 주입하고 CRITICAL/UI/audit 표시를 켠다.
- [ ] trusted proxy header+비loopback 위험 조합을 실패시킨다.
- [ ] 비loopback+빈 origin allowlist를 실패시킨다.
- [ ] TLS cert/key mismatch, 만료, chain load, secret permission 실패에서 HTTP downgrade 없이 종료한다.
- [ ] 구성 행렬을 table-driven startup test로 만든다.

### S10-05. HTTP thread/queue 자원 예산

- [ ] HTTP base/max thread, queued requests, active SSE 상한을 유한값으로 설정한다.
- [ ] thread 수가 `max streams + peak REST + margin`을 만족하는지 startup에서 검사한다.
- [ ] HTTP request queue와 Core command queue를 분리한다.
- [ ] 명령 queue 기본 64, SSE 기본 16, rate limit 기본 60/subject/min 또는 승인값을 적용한다.
- [ ] queue full은 무한 대기 대신 429, stream 초과는 503을 반환한다.
- [ ] body/headers/URI/JSON depth 및 key/bytes 상한을 둔다.
- [ ] active/queued/rejected/latency를 metric/OpsLogger로 노출한다.
- [ ] 16 SSE+REST burst에서 starvation/무제한 memory 증가가 없는지 부하 test한다.

### S10-06. Authentication bootstrap와 Subject

- [ ] 승인 auth profile에 따라 공개 bootstrap path의 exact allowlist를 구현한다.
- [ ] 정적 asset을 포함한 나머지 path는 unauthenticated 접근을 거부한다.
- [ ] session fixation을 막고 login/step-up 시 session ID를 회전한다.
- [ ] cookie 사용 시 Secure, HttpOnly, 승인된 SameSite, 좁은 Path, TTL/폐기 규칙을 적용한다.
- [ ] mTLS/badge/OIDC claim을 stable subject/roles/auth method/line ID로 server-side map한다.
- [ ] body/query/임의 header의 subject override를 무시/거부한다.
- [ ] accepted auth method와 role/line allowlist를 적용한다.
- [ ] logout/session expiry 뒤 pending approval 자체는 Core store에 유지한다.
- [ ] spoofed claim/header/session fixation/expired session test를 만든다.

### S10-07. Authorization, Origin, CSRF, step-up

- [ ] GET도 모두 인증하고 audit는 `audit_reader`만 허용한다.
- [ ] command별 role matrix를 만들고 queue 제출 전에 403으로 거부한다.
- [ ] POST Origin을 필수로 하고 exact scheme/host/port allowlist 비교를 한다.
- [ ] `Sec-Fetch-Site`의 허용값을 고정한다.
- [ ] `application/json` 이외 simple request를 415로 거부한다.
- [ ] CSRF token을 auth session에 결합하고 CSPRNG/constant-time compare/rotation/expiry를 구현한다.
- [ ] token을 query/body로 받지 않고 `X-Cogito-CSRF` header만 허용한다.
- [ ] approve/reject/seal/ack/finalize retry에 subject/method/TTL이 유효한 step-up을 요구한다.
- [ ] step-up 실패 시 nonce 소비, queue 제출, state change가 0회인지 확인한다.
- [ ] auth/CSRF token/nonce/PIN/password를 로그에서 canary 검색한다.

### S10-08. CommandQueue

- [ ] command envelope에 UUIDv4 command ID, route/type, Subject snapshot, payload digest, submit time, completion channel을 넣는다.
- [ ] P1 cancel/approve/reject, P2 ack/seal/retry finalize, P3 turn으로 우선순위를 고정한다.
- [ ] 같은 priority 안에서 FIFO를 보장한다.
- [ ] queue length를 64 또는 config 상한으로 제한한다.
- [ ] worker의 ack wait를 기본 5초로 제한한다.
- [ ] 5초를 넘으면 queued 202를 반환하되 command를 취소하지 않는다.
- [ ] future/HTTP timeout이 turn cancel을 뜻하지 않게 한다.
- [ ] chat 폭주 중 P1/P2 기아가 없는지 virtual clock/load test한다.
- [ ] shutdown 시 queued/running command 처리 정책을 정의한다.

### S10-09. AgentLoop 전용 thread와 cancel

- [ ] agent-loop-only ABI 호출은 `agent_thread.cpp` 한 곳에서만 수행한다.
- [ ] HTTP handler source에서 owner-only symbol 호출을 정적 검사로 금지한다.
- [ ] dequeued command에 제출 당시 Subject snapshot을 전달한다.
- [ ] command 처리 exception을 bounded result로 바꾸고 consumer thread를 살린다.
- [ ] owner idle wait가 `min(1000ms, approval_timeout/10)` tick을 수행한다.
- [ ] G0-07의 cancel atomic flag 예외와 owner-thread 감사/전이 순서를 구현한다.
- [ ] 요청 abort/tab close/SSE disconnect가 cancel flag를 세우지 않는지 test한다.
- [ ] SIGTERM/service stop의 listener stop→queue→active turn→audit flush→owner destroy 순서를 Runbook과 맞춘다.

### S10-10. Command idempotency와 TurnOutcome store

- [ ] command ID를 Subject/route/canonical payload digest와 결합한다.
- [ ] 같은 ID+같은 payload는 실행 없이 원래 ack/result reference를 반환한다.
- [ ] 같은 ID+다른 payload는 409로 처리하고 두 payload 모두 실행하지 않는다.
- [ ] 승인된 TTL/최근 256개/LRU 또는 FIFO 정책을 적용한다.
- [ ] process restart 뒤 cache 의미를 API에 명시한다.
- [ ] TurnOutcome store의 count/bytes/TTL 상한과 eviction response를 구현한다.
- [ ] pending approval는 browser/session/cache와 독립적으로 Core에 남는다.
- [ ] 동시 duplicate approve/turn submit에서 실제 실행 횟수를 검사한다.

### S10-11. 공통 HTTP contract/parser/error

- [ ] 모든 body를 Strict JSON/UTF-8/duplicate-key/size/depth/key limit로 검사한다.
- [ ] route별 request/response JSON Schema를 적용하고 unknown field를 거부한다.
- [ ] 공통 error envelope에 stable code/reason code/request ID를 넣고 내부 detail을 숨긴다.
- [ ] 400/401/403/409/413/415/429/500/503 mapping을 contract test로 고정한다.
- [ ] `application/json; charset=utf-8` 허용 여부를 문서와 parser에서 일치시킨다.
- [ ] error path에도 CSP/nosniff 등 공통 header를 적용한다.
- [ ] route fuzz가 Core/queue에 malformed command를 전달하지 않는지 검사한다.

### S10-12. POST 7개 구현

- [ ] `POST /api/turn`은 P3 queue에 넣고 202+command ID/state만 반환한다.
- [ ] `POST /api/approve`는 ID+digest+nonce+step-up+SOD 검사 후 P1 queue에 넣고 turn 결과 없이 202를 반환한다.
- [ ] `POST /api/reject`는 bounded reason과 동일 승인 검증을 적용한다.
- [ ] `POST /api/cancel`은 명시적 요청만 취소로 처리한다.
- [ ] `POST /api/indeterminate/ack`는 nonempty bounded note와 step-up을 요구한다.
- [ ] `POST /api/finalize/retry`는 finalize-pending일 때만 queue에 넣는다.
- [ ] `POST /api/session/seal`은 비가역 의미를 response/UI에 표시하고 step-up을 요구한다.
- [ ] 모든 POST에 command ID/idempotency/auth/origin/CSRF/content-type/rate limit를 공통 적용한다.
- [ ] route별 invalid state는 Core state를 바꾸지 않는 409/승인 error로 매핑한다.

### S10-13. GET 8개 구현

- [ ] state, pending approvals, tools, budget, transitions는 C ABI snapshot만 사용한다.
- [ ] audit는 `audit_reader`와 Core query ABI만 사용한다.
- [ ] events는 next-events ABI를 SSE로 노출한다.
- [ ] turns/{turn_id}는 bounded host outcome store만 조회한다.
- [ ] host code에 `sqlite3_open("audit.db")` 또는 내부 Core object cast가 0건인지 정적 검색한다.
- [ ] 모든 ABI buffer를 RAII로 free한다.
- [ ] query authorization과 no-store cache policy를 path별 test한다.

### S10-14. Audit read path 보호

- [ ] Core read-only connection은 `PRAGMA query_only=ON`과 동일 authorizer를 쓴다.
- [ ] page를 최대 200행, concurrent query를 2개, read transaction을 2초로 제한한다.
- [ ] payload 원문 대신 승인 field projection만 반환한다.
- [ ] 조회 subject, filter/range, result count, latency를 redacted OpsLogger에 남긴다.
- [ ] WAL size/disk free 임계 초과 시 audit query를 먼저 차단한다.
- [ ] query 부하 중 `tool_call_started` commit이 성공하거나 실패 시 write 0회인지 검사한다.
- [ ] audit reader가 아닌 역할, oversized range, timeout에서 writer 영향이 bounded인지 확인한다.

### S10-15. SSE 구현

- [ ] cpp-httplib `set_chunked_content_provider` 위에 SSE를 직접 구현한다.
- [ ] event ID는 audit seq 하나만 사용하고 별도 global sequence를 만들지 않는다.
- [ ] payload를 안전한 JSON data line으로 인코딩해 CR/LF frame injection을 막는다.
- [ ] `Last-Event-ID`를 strict bounded integer로 파싱한다.
- [ ] reconnect 시 `next_events(from_seq)`로 유실/중복 없이 재생한다.
- [ ] `process_epoch_id`가 바뀌면 replay를 중단하고 state/approvals/budget을 전체 조회한다.
- [ ] 15초마다 `: keepalive`를 보내고 dead/slow client를 종료한다.
- [ ] 동시 16 stream, batch/bytes/wait 상한과 503을 적용한다.
- [ ] client→server frame/command parser 및 WebSocket upgrade를 거부한다.
- [ ] browser 0명일 때 생긴 pending approval가 GET에서 보존되는지 검사한다.

### S10-16. 보안 header와 CSP

- [ ] 앱이 `default-src 'none'`, self script/style/connect, object/base/form/frame 제한 CSP를 직접 발급한다.
- [ ] `frame-ancestors 'none'`, HSTS, nosniff, no-referrer를 성공/오류 response에 적용한다.
- [ ] `/api/*`에 `Cache-Control:no-store`를 적용한다.
- [ ] wildcard CORS와 Origin reflection을 금지한다.
- [ ] production bundle을 정확한 CSP로 조기 실행하고 console violation 0을 확인한다.
- [ ] inline style 때문에 불가하면 unsafe-inline을 바로 추가하지 않고 G0-33의 정적 SVG/대안을 적용한다.
- [ ] clickjacking frame embed와 외부 image beacon을 browser test한다.

### S10-17. Frontend package 고정

- [ ] React/DOM 18, Tailwind 4, Radix Dialog, Zustand 4, Lucide 명명 import를 lock한다.
- [ ] xyflow 12는 실시간 graph가 필요할 때만, TanStack Table은 explicit major, Virtual 3을 고정한다.
- [ ] Recharts 3을 쓰면 `react-is`를 direct dependency로 넣는다.
- [ ] react-markdown 10과 remark-gfm 4를 고정한다.
- [ ] Tremor, Monaco, diff-viewer, raw HTML plugin, runtime CDN을 dependency/lint에서 금지한다.
- [ ] floating `latest`, git URL, unpinned tarball dependency가 0인지 검사한다.
- [ ] clean `npm ci`와 lockfile integrity를 CI에서 확인한다.

### S10-18. 타입 안전 API/state 계층

- [ ] server contract에서 TypeScript type을 생성하거나 contract test로 동기화한다.
- [ ] command client가 UUID, CSRF, credentials, error envelope를 한 곳에서 처리한다.
- [ ] UI는 표시 문자열이 아니라 reason code로 분기한다.
- [ ] AbortSignal은 HTTP request만 중단하고 cancel endpoint를 자동 호출하지 않는다.
- [ ] initial/reconnect마다 state, pending approvals, budget을 권위 조회한다.
- [ ] SSE event를 최종 권위로 오인하지 않고 seq/epoch를 검사한다.
- [ ] component가 임의 fetch/identity header를 만들지 못하게 lint/module boundary를 둔다.

### S10-19. SafetyBanner 우선 구현

- [ ] insecure mode, sealed, finalize pending, line write lockdown, indeterminate 상태를 상단 고정으로 표시한다.
- [ ] “승인 기반 상위 수준 작업 — 안전 계통은 기존 인증 체계가 담당” 문구를 상시 표시한다.
- [ ] 색상뿐 아니라 텍스트와 icon/ARIA로 위험을 전달한다.
- [ ] 위험 banner가 modal/chart/scroll에 가려지지 않게 한다.
- [ ] 모든 state 조합 snapshot/visual test를 만든다.

### S10-20. 승인 권위 영역

- [ ] tool name, effect, risk, coverage, approval required를 Core field 그대로 표시한다.
- [ ] CCJ arguments, before, policy rule ID, digest 앞 16자, expiry, requester를 표시한다.
- [ ] 1024×768에서 접기/내부 scroll 없이 상단에 모두 보이게 한다.
- [ ] effect는 색과 “설비 변경·위험도 …” 텍스트를 함께 쓴다.
- [ ] coverage가 full이 아니면 “생성 단계 제약 없음 — 런타임 검증만 적용”을 표시한다.
- [ ] 실행 전 존재하지 않는 after를 승인 화면에 만들지 않는다.
- [ ] model/RAG/tool text가 권위 field나 button label에 한 글자도 들어가지 않게 한다.
- [ ] before/requested는 외부 diff renderer 대신 scalar 권위 표로 표시한다.

### S10-21. Untrusted text와 승인 조작 UX

- [ ] 외부 유래 text는 별도 `UntrustedText`에서 pre-wrap 평문으로만 렌더한다.
- [ ] provenance와 “신뢰할 수 없는 외부 데이터” label을 표시한다.
- [ ] link/image/Markdown/HTML을 승인 panel에서 사용하지 않는다.
- [ ] approve/reject button을 24px 이상 떨어뜨리고 touch target을 56×56px 이상으로 한다.
- [ ] approve는 2단계 확인과 step-up을 거친다.
- [ ] 202를 실행 성공으로 표시하지 않고 verdict/event/query 뒤에 최종 상태를 표시한다.
- [ ] reentry exceeded, lockdown, expired, policy denied를 구분한다.
- [ ] double-click, keyboard, touch, expiry race에서 실제 write count를 검사한다.

### S10-22. 서버 monotonic expiry

- [ ] server가 제공한 expires-in/monotonic anchor만 승인 가능 시간의 권위로 사용한다.
- [ ] browser `Date.now()`를 authorization 판단이나 표시 anchor로 사용하지 않는다.
- [ ] 표시 감소에는 필요 시 `performance.now()`를 쓰되 server 재동기화한다.
- [ ] PC wall clock을 앞뒤로 바꿔도 실제 expiry와 UI가 어긋나지 않는지 test한다.
- [ ] expiry 뒤 click은 Core에서 다시 거부되고 write 0회인지 확인한다.

### S10-23. 상태, 예산, 감사, FSM 화면

- [ ] Budget 화면은 Core snapshot의 limit/reserved/used/deadline만 사용한다.
- [ ] AuditTable은 server pagination과 field projection만 사용한다.
- [ ] FSM graph는 `/api/transitions` 결과에서만 node/edge를 만든다.
- [ ] R1/R2 universal transition도 표시한다.
- [ ] source에 hardcoded FSM edge 목록이 없는지 정적 검사한다.
- [ ] interactive graph가 불필요하거나 CSP 문제가 있으면 build-time CLI 출력 기반 SVG를 사용한다.
- [ ] core transition fixture 변경 시 UI가 자동 반영되는 contract test를 둔다.

### S10-24. Markdown hardening M1~M7

- [ ] `rehype-raw`, `rehype-katex`, `dangerouslySetInnerHTML`, `react/no-danger` 위반을 lint/CI로 실패시킨다.
- [ ] URL transform은 반드시 `defaultUrlTransform` 결과에 추가 same-origin 제한을 합성한다.
- [ ] javascript/data/vbscript/protocol-relative/cross-origin HTTPS/유사 domain을 거부한다.
- [ ] image component를 `() => null`로 바꾸고 CSP와 이중 방어한다.
- [ ] `allowedElements` allowlist만 사용한다.
- [ ] render 직전 UTF-8 256KiB와 AST 5,000 node 상한을 적용한다.
- [ ] 초과 시 안전한 절단 표시를 하고 multi-byte 경계를 깨지 않는다.
- [ ] message별 nonempty unique clobber prefix를 적용한다.
- [ ] ApprovalPanel subtree에 ReactMarkdown이 0인지 dependency/DOM test한다.

### S10-25. Embedded assets와 digest

- [ ] x86-64 build host에서 `npm ci`→Vite build→gzip/embed를 수행한다.
- [ ] route/MIME/content bytes를 deterministic order로 `assets_embedded.cpp`에 만든다.
- [ ] production dist 전체의 승인된 canonical manifest와 SHA-256을 `dist.sha256`에 기록한다.
- [ ] binary runtime이 embedded bytes/manifest를 재hash하고 불일치 시 startup 실패한다.
- [ ] same digest를 Core initialization으로 주입해 state와 turn_begin에 기록한다.
- [ ] 비Web build의 assets digest 표현을 계약대로 고정한다.
- [ ] runtime filesystem dist mount/path override/path traversal을 금지한다.
- [ ] embedded route 404/MIME/cache/security header를 test한다.

### S10-26. Offline와 npm SBOM/license

- [ ] Jetson/target에서 Node/npm/Tailwind oxide 실행이 0회인지 network/process audit로 확인한다.
- [ ] offline browser network log에서 외부 request/CDN/font/icon fetch가 0건이다.
- [ ] Vite/Rollup manifest 기준으로 실제 bundle 포함 package를 식별한다.
- [ ] npm CycloneDX BOM을 C++/vcpkg BOM과 merge한다.
- [ ] build-only와 runtime-distributed package를 구분한다.
- [ ] vendored shadcn source와 picosha2를 별도 third-party inventory에 넣는다.
- [ ] SPDX OR, SEE LICENSE, 구식 ID, UNLICENSED, missing field의 unknown 판정을 fail 처리한다.
- [ ] package import add/remove가 SBOM diff에 반영되는지 test한다.

### S10-27. Web 필수 테스트 12개

- [ ] `web/auth`: unauth asset/API 거부, role 거부, insecure side effect 전체 Deny, 위험 bind startup 실패.
- [ ] `web/csrf`: Origin 없음/불일치, text/plain, CSRF 누락·타 session 모두 write 0회.
- [ ] `web/single_command_path`: SSE approve 유사 data 상태 무변경, WebSocket upgrade 거부.
- [ ] `web/thread_affinity`: worker owner-only call은 wrong-thread+상태 무변경, TSan clean.
- [ ] `web/approval_ui`: prompt injection이 권위 영역에 0글자, expiry click write 0회.
- [ ] `web/sse_resume`: 유실/중복 0, epoch 변경 full refresh, no-client pending 보존.
- [ ] `web/lifetime`: abort/tab close는 cancel 아님, turn_end 정확히 1회.
- [ ] `web/idempotency`: duplicate command 재실행 0, double approve write 1회, payload conflict.
- [ ] `web/backpressure`: queue full 429, priority starvation 없음, stream 초과 503.
- [ ] `web/audit_readpath`: role/page/time/concurrency limit, writer 보존 또는 write 0회.
- [ ] `web/csp`: 모든 response header와 browser violation 0, frame 차단.
- [ ] `web/offline`: network 차단 상태에서 외부 request 0, 전체 dashboard 동작.

### S10-28. 추가 Web 보증 테스트

- [ ] API contract 전 route schema/status/error envelope test를 만든다.
- [ ] step-up subject/method/TTL boundary test를 만든다.
- [ ] rate/body/connection limit test를 만든다.
- [ ] Markdown M1~M7 adversarial fixture를 만든다.
- [ ] embedded byte/digest/tamper boot-fail test를 만든다.
- [ ] finalize retry/seal/ack/cancel recovery end-to-end test를 만든다.
- [ ] graceful shutdown과 process epoch refresh test를 만든다.
- [ ] HTTP JSON, Last-Event-ID, URL transform을 fuzz한다.

### S10-29. 현장 UI 인수

- [ ] 승인 field와 banner가 1024×768에서 scroll 없이 보이는지 대상 panel에서 확인한다.
- [ ] 1280×800, 승인된 browser/version/Vite target에서도 확인한다.
- [ ] glove touch 56×56/24px, keyboard, screen reader, color-blind 조건을 확인한다.
- [ ] 한국어 UI가 reason code로 분기하고 raw code fallback을 제공한다.
- [ ] 공장 현지시각+timezone을 표시하고 감사 원본은 UTC인지 확인한다.
- [ ] 교대 logout/session expiry 뒤 다음 operator가 pending approval를 조회할 수 있다.
- [ ] OT segment bind와 origin allowlist가 승인 network 범위를 넘지 않는다.
- [ ] 화면에 Cogito++가 안전 PLC/HMI를 대체한다는 오해 문구가 0건이다.

**S10 Exit Gate**

- [ ] W1~W22가 code/test/threat model에 모두 추적된다.
- [ ] auth/CSRF/step-up/SOD/queue/thread 경계 우회 경로가 0건이다.
- [ ] 필수 12개와 추가 security test, ASan/UBSan/TSan, browser CSP test가 통과한다.
- [ ] approval 권위 영역에 external text가 0글자이고 1024×768 실기를 통과한다.
- [ ] WebSocket, host DB 직접 접근, filesystem asset serving, forbidden Markdown API가 0건이다.
- [ ] offline target/browser에서 외부 network와 Node/npm 실행 0회다.
- [ ] embedded bytes digest = `dist.sha256` = state = `turn_begin`이다.
- [ ] C++/npm 통합 SBOM과 license gate가 통과한다.
