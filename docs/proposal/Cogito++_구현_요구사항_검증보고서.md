# Cogito++ 구현 요구사항 검증 보고서

**대상 문서** — `Cogito++_구현_요구사항.md` (검증 반영 구현 기준서 초안, 2026-08-20)
**검증일** — 2026-08-20
**검증 범위** — ① 인용된 1차 자료와의 사실관계 대조 ② 문서 내부 정합성 ③ 지목된 코드 결함의 재현 여부 ④ 구현 착수 가능 여부
**검증 방식** — 공식 저장소·규격 문서 직접 조회 (추정·기억 기반 판단 배제)

---

## 0. 총평

| 항목 | 결과 |
| --- | --- |
| **사실관계 주장** | **12건 검증 → 12건 모두 정확.** 오류·과장 없음 |
| **원문 코드 결함 지적** | **10건 검증 → 10건 모두 실재.** 오탐 0건 |
| **문서 자체 결함** | **17건 발견** — 🔴 계약 미완 5건, 🟠 중대 6건, 🟡 보완 6건 |
| **구현 착수 판정** | **조건부 가능.** 🔴 5건은 P0 코드 작성 전 해소 필요 (전부 문서 수정으로 해결 가능, 설계 변경 불필요) |

이 문서는 제가 작성한 `Cogito++_OSS_기술스택_아키텍처.md`의 과장·오류를 **정확하게** 잡아냈습니다. 특히 GBNF, `additionalProperties`, WAL, `-fno-exceptions`, MCP `initialize`, vcpkg 포트명 지적은 전부 1차 자료로 확인됩니다. 아래 §1의 정정 사항은 제 원문이 틀린 것이며, 아키텍처 문서를 수정해야 합니다.

다만 이 문서는 **"조사 자료"에서 "구현 계약"으로 승격되는 과정에서 생긴 미완결 지점**이 있습니다. §3이 그 목록이며, 이것이 이번 검증의 실질적 산출물입니다.

---

## 1. 사실관계 검증 — 12건 전부 정확

각 항목을 1차 자료로 직접 확인했습니다.

| # | 문서의 주장 | 검증 결과 | 근거 |
| --- | --- | --- | --- |
| 1 | MCP 2026-07-28은 `initialize`를 제거했다 | ✅ **정확** | 규격 원문: *"There is no negotiation handshake. Every request carries its protocol version."* **Modern**(2026-07-28+) = per-request `_meta`, **Legacy**(2025-11-25 이하) = `initialize` 핸드셰이크로 명시적 분리 |
| 2 | `server/discover`를 사용한다 | ✅ **정확** (단, §3-O 참조) | 규격: *"Servers **MUST** implement `server/discover`."* |
| 3 | 2025-11-25 호환은 별도 legacy profile로 격리 | ✅ **정확** | 규격의 dual-era 호환성 매트릭스와 일치 |
| 4 | MCP 기본 방언은 JSON Schema 2020-12 | ✅ **정확** | 규격 스키마 기준 |
| 5 | `JSON_NOEXCEPTION`은 오류를 abort시킨다 | ✅ **정확** (오히려 보수적 표현) | 공식 문서: *"`throw` is replaced by `std::abort()`"* — 문서의 "일부 경로"보다 실제는 **모든 throw 경로**. 결론은 동일하게 성립 |
| 6 | llama.cpp GBNF는 JSON Schema 부분집합만 지원 | ✅ **정확** (§3-A에서 더 강화 필요) | `grammars/README.md`가 미지원 목록을 명시 |
| 7 | vcpkg 포트명은 `json-schema-validator` | ✅ **정확** | vcpkg 매니페스트 확인 (v2.4.0). 제 원문의 `nlohmann-json-schema-validator`는 **오류** |
| 8 | CMake 타깃 `nlohmann_json_schema_validator::validator` | ✅ **정확 — "확인" 딱지 제거 가능** | 상류 CMakeLists 확인: `find_package(nlohmann_json_schema_validator)`, `add_library(nlohmann_json_schema_validator::validator ALIAS ...)` |
| 9 | open62541 vcpkg feature는 `openssl` | ✅ **정확** | 포트 v1.4.14 feature 전체: `diagnostics, discovery, historizing, mbedtls, methodcalls, multithreading, openssl, subscriptions, subscriptions-events`. 제 원문의 `encryption-openssl`은 **존재하지 않음** |
| 10 | Paho는 `EPL-2.0 OR BSD-3-Clause` | ✅ **정확** | EDL-1.0 = BSD-3-Clause. 문서의 SPDX 표기가 제 원문("EDL-1.0")보다 정확 |
| 11 | CRA 보고 의무 2026-09-11 | ✅ **정확** | EU 공식: *"As of 11 September 2026, manufacturers are required to report actively exploited vulnerabilities and severe incidents."* |
| 12 | CRA 주요 의무 2027-12-11 | ✅ **정확** | EU 공식: 발효 2024-12-10, 주요 의무 2027-12-11 |

### 1-1. WAL / `additionalProperties` / GBNF 정정에 대하여

문서 §1-2의 나머지 정정도 전부 타당합니다.

- **WAL ≠ append-only** — 맞습니다. WAL은 내구성/동시성 저널 모드이며 `UPDATE`/`DELETE`를 막지 않습니다. 제 원문이 두 개념을 혼동했습니다.
- **`additionalProperties:false`는 거부이지 제거가 아니다** — 맞습니다. JSON Schema 검증은 문서를 변형하지 않습니다. 제 원문의 "잉여 필드를 제거한다"는 명백한 오류입니다.
- **"같은 입력이면 같은 실행 경로"의 조건 누락** — 맞습니다. §14-6의 골든 키 정의가 올바른 수정입니다 (단 §3-N 참조).
- **`minimum`/`maximum` ≠ 물리적 안전 한계** — 맞습니다. 정적 입력 범위와 운전 상태·변경률·interlock은 다른 층위입니다.

---

## 2. 지목된 코드 결함 10건 — 전부 실재

제 원문 스켈레톤을 재검토한 결과 10건 모두 재현됩니다. 오탐은 없습니다.

| # | 지적 | 재현 | 원인 위치 |
| --- | --- | --- | --- |
| 1 | Done/Failed에서 다음 턴 시작 전이 없음 | ✅ | `kTransitions` 17개 중 `Done`/`Failed`를 `from`으로 하는 항목 0개 |
| 2 | 다중 Action 처리 시 두 번째부터 깨짐 | ✅ | 내부 루프가 `gate_.Evaluate(action, fsm_.current(), ...)` 호출. 1회차 후 상태가 `Observe`가 되어 게이트 ②단계에서 `Deny` |
| 3 | `Infer`에서 `BudgetExhausted` 전이 없음 | ✅ | 예산 체크는 `Infer` 상태에서 발화하나, 표에는 `{Observe, BudgetExhausted, Done}`만 존재 |
| 4 | `Fsm::Fire()` 오류를 전 호출부가 무시 | ✅ | `Error`를 반환하지만 `[[nodiscard]]` 없음. 호출부 12곳 모두 반환값 미사용 |
| 5 | 추론 오류 조기 반환에서 `turn_end` 누락 | ✅ | `return resp.error();`가 `audit_.TurnEnd(...)` 이전에 위치 |
| 6 | OPC 매니페스트 `risk`/`requires_approval` 무시 | ✅ | `BuildReadTool`/`BuildWriteTool`이 `Risk::Read`/`Risk::Write`를 하드코딩. `requires_approval`은 설명 문자열로만 소비 |
| 7 | 금지 노드가 `NotRegistered`로 먼저 반환됨 | ✅ | `Describe()`가 `forbidden`을 `continue`로 제외 → 게이트 ①단계에서 `NotRegistered`. `ForbiddenRules()`의 상세 사유는 도달 불가 |
| 8 | C ABI 콜백 문자열 소유권 불명확 | ✅ | `cogito_tool_fn(..., char** out_json, ...)`의 해제 주체가 헤더에 미정의. 호스트 CRT ↔ 코어 CRT 불일치 시 힙 손상 |
| 9 | C++17 프로젝트에 C++20 지정 초기화자 | ✅ | `main.cpp`의 `Limits{ .max_turns = 8, ... }`. MSVC `/std:c++17`에서 컴파일 실패 |
| 10 | 설치 패키지 미완결 | ✅ | `install(EXPORT cogitoTargets ...)`는 있으나 `configure_package_config_file()`로 `cogitoConfig.cmake`를 생성하지 않음. 선택 어댑터 타깃은 `install(TARGETS)`에 미포함 |

**결함 7번은 특히 중요합니다.** 이것은 단순 버그가 아니라 게이트 순서 설계의 구조적 문제입니다. 문서가 §5-2에서 `status: forbidden` tombstone을 도입해 해결한 것은 올바른 처방입니다.

---

## 3. 이 문서 자체에서 발견한 결함 — 17건

여기서부터가 새로운 내용입니다. **§1·§2가 확인 작업이라면, 이 절이 이번 검증의 산출물입니다.**

---

### 🔴 A. `§6` Tool Schema 부분집합과 GBNF 부분집합이 불일치 — 대표 예시가 미지원 케이스

**가장 중요한 발견입니다.**

문서는 §1-2에서 "llama.cpp가 지원하는 부분집합만 생성 단계에서 제한한다"고 옳게 정정했습니다. 그러나 **§6이 정의한 Cogito Tool Schema 부분집합과 GBNF가 실제로 표현 가능한 부분집합을 대조하지 않았습니다.** llama.cpp 공식 문서를 확인한 결과 다음 격차가 있습니다.

| §6이 허용한 키워드 | GBNF 실제 지원 |
| --- | --- |
| `minimum`/`maximum` (type: **number**) | ❌ **미적용** — 공식 문서: *"Numeric bounds only work for `integer`, not `number`"* |
| `exclusiveMinimum`/`exclusiveMaximum` | ❌ 지원 언급 없음 |
| `const` | ❌ 지원 언급 없음 (`enum`은 표현 가능) |
| `minItems`/`maxItems` | ❌ 지원 언급 없음 |
| `pattern` | ⚠️ **`^`로 시작하고 `$`로 끝나야만 동작** |
| `minLength`/`maxLength` | ✅ 지원 |
| `additionalProperties:false` | ✅ (GBNF 변환기의 기본값이 이미 `false`) |

**결과적으로 원문의 대표 예시가 정확히 미지원 케이스입니다.**

```json
"value": { "type": "number", "minimum": 0.70, "maximum": 0.95 }
```

OPC UA 검사 임계값 — `number` + `minimum`/`maximum` 조합은 GBNF가 제약하지 못합니다. 문법 생성은 **실패하지 않고 성공하며, 범위 제약만 조용히 누락**됩니다.

이 때문에 §10-2의 **"grammar 생성 실패 시 비제약 생성으로 조용히 폴백하지 않는다"는 요구가 이 상황을 잡아내지 못합니다.** 잡을 실패 자체가 발생하지 않기 때문입니다. 불변식 3(항상 런타임 재검증)이 있어 **안전은 유지되지만**, "생성 단계에서 제한한다"는 서술은 실제 도구 대부분에서 참이 아니게 됩니다.

**조치 (문서 수정으로 해결 가능)**

1. **Tier-G 부분집합**을 §6에 신설 — 검증기와 GBNF가 **모두** 강제할 수 있는 교집합.
2. 부팅 시 스키마 컴파일러가 도구별로 `grammar_coverage ∈ {full, partial, none}`를 산출하고 **감사에 기록**한다.
3. `partial`/`none` 도구는 문서·UI·감사에서 **비제약 생성으로 취급**한다. "생성 단계 제한"을 주장하지 않는다.
4. §6 금지 목록을 GBNF 실제 제약에 맞춰 보강: `not`, `if`/`then`/`else`, `dependentSchemas`, `uniqueItems`, `contains`/`minContains`, `patternProperties`, `prefixItems`, `$anchor`, `format`. 그리고 `pattern`은 **`^...$` 앵커를 필수**로 한다.
5. §14에 테스트 추가: "§6 허용 키워드 전체에 대해 GBNF 변환 후 제약이 실제로 적용되는지 확인하고, 미적용 키워드가 `grammar_coverage`에 정확히 반영되는지 검사."

---

### 🔴 B. 코어 의존성 "두 개" 주장이 `§5-3`과 모순 — 코어에 SHA-256이 필요하다

| 위치 | 서술 |
| --- | --- |
| §1-1 | 코어 의존성은 `nlohmann/json`과 `json-schema-validator` **두 개** |
| §4-1 | `cogito_core` 직접 의존성 = 위 두 개. **SHA-256은 `cogito_audit_sqlite`에 배치** |
| §5-3 | `action_digest = SHA-256(...)`, `permit_scope_digest = SHA-256(...)` |
| §5-3 | `ExecutionPermit`은 **"코어만 생성할 수 있는"** 객체 |
| §8-1 | 10단계 Permit 발급 = `PermissionGate` = 코어 |
| §5-1 | `raw_digest`: SHA-256 |
| §5-4 | ApprovalRecord가 `action_digest`·`permit_scope_digest`에 결합 |

**Permit·Approval·Action digest는 전부 코어에서 계산되어야 하는데, SHA-256이 코어 밖에 있습니다.** 감사 타깃을 링크하지 않은 `cogito_core` 단독 빌드는 §5-3을 구현할 수 없습니다. 이는 §15 Phase 1의 "설치 가능한 core package 생성"을 직접 막습니다.

**조치** — §12-1이 이미 "SHA-256 구현 / P0 / 소스 확보·라이선스·핀 명시"로 P0에 두었으므로, §4-1에서 `cogito_core`의 직접 의존성으로 이동하고 §1-1을 **"두 개 + 벤더링된 SHA-256 구현"** 으로 정정하면 됩니다. 헤더 온리 단일 파일(예: picosha2, MIT)이면 "경량 코어" 주장은 유지됩니다.

---

### 🔴 C. 승인 후 재평가가 두 곳에 서로 다르게 정의됨 + Ask verdict의 감사 커밋 시점 부재

**모순 1 — 재평가 메커니즘이 이중 정의**

| §7-2 (FSM) | `AwaitApproval | Approved | Gate` → **Gate 상태로 재진입** |
| §8-1 (게이트 순서) | 8단계 "승인 후 2~7단계 재평가" → **단일 `Evaluate()` 호출 내부 루프** |

두 서술은 양립하지 않습니다. §8-1대로면 `Evaluate()` 안에서 승인을 기다리므로 FSM이 `AwaitApproval`로 전이할 수 없고 (그리고 §11-3의 비동기 승인 API와도 충돌), §7-2대로면 8단계는 별도 단계가 아니라 "Gate 재진입 시 1~7단계를 처음부터 다시 수행"이 됩니다.

**모순 2 — Ask verdict를 언제 감사에 커밋하는가**

§8-1은 감사 커밋을 **9단계**에 둡니다. 그런데 `Ask` 판정은 8단계에서 `AwaitApproval`로 빠져나가므로 **9단계에 도달하지 않습니다.** 그러면:

- §9-1이 필수로 규정한 `verdict`·`approval_requested` 이벤트의 기록 시점이 정의되지 않고,
- §7-2의 `AwaitApproval | AuditError | Failed` 전이가 어떤 커밋의 실패를 가리키는지 알 수 없으며,
- 불변식 12(모든 경로에서 `turn_end` 정확히 1회)의 검증 대상이 흐려집니다.

`Deny` 경로도 동일한 문제를 갖습니다 (`Gate | Deny | Observe`로 빠지므로 9단계 미도달).

**조치**

1. **§7-2의 FSM 재진입을 규범으로 채택**하고 §8-1의 8단계를 삭제, 9·10단계를 8·9로 재번호.
2. 감사 커밋 규칙을 다음으로 명시:
   - **모든 verdict(Allow/Ask/Deny)는 상태 전이 이전에 커밋**한다.
   - `Ask` 시 `approval_requested`를 `AwaitApproval` 전이 이전에 커밋한다.
   - `Allow` 시 `tool_call_started`를 **Permit 발급 이전에** 커밋한다 (불변식 7 충족).
3. Gate 재진입 시 1~7단계를 **전부 다시** 수행함을 명시 (불변식 6의 구현 형태 확정).

---

### 🔴 D. FSM 전이 누락 — `§14-1`이 요구하는 테스트를 통과할 수 없다

§7-2의 전이표는 `Cancel`과 `AuditError`를 **비대칭적으로만** 정의합니다.

| 상태 | `Cancel` | `AuditError` |
| --- | --- | --- |
| `Idle` | ❌ 없음 | — |
| `Infer` | ✅ 있음 | ❌ **없음** (`inference_requested`/`inference_result` 기록 실패 시 갈 곳 없음) |
| `Propose` | ❌ 없음 | — |
| `Gate` | ❌ **없음** | ✅ 있음 |
| `AwaitApproval` | ✅ 있음 | ✅ 있음 |
| `Execute` | ❌ **없음** | ✅ 있음 |
| `Observe` | ❌ **없음** | ❌ **없음** (`tool_result`/`transition` 기록 실패 시 갈 곳 없음) |

이로 인한 직접적 충돌:

- §14-1이 **"취소 종료 경로"** 테스트를 요구하지만, §5-5의 `ToolResult.status = cancelled`를 만들어내는 `Execute` 중 취소에 대응하는 전이가 없습니다.
- §7-3이 "추론·도구·승인에 deadline과 cancellation token을 전달한다"고 하지만, 도구 실행 중 취소가 발생하면 §7-3의 "정의되지 않은 전이는 즉시 `Failed`" 규칙에 걸려 **정상 취소가 오류로 기록**됩니다.

**조치** — 전이표 대신 규칙으로 정의하는 편이 간결합니다.

```
[규칙 1] AuditError는 모든 비종료 상태에서 Failed로 전이한다.
[규칙 2] Cancel은 모든 비종료 상태에서 Cancelled로 전이한다.
         단 Execute 중 취소는 예외:
           - effect == none        → ToolResult{cancelled} 기록 후 Observe
           - effect != none        → ToolResult{indeterminate} 기록 후 Observe
             (불변식 9: 결과가 불명확한 write는 취소가 아니라 indeterminate)
[규칙 3] 위 규칙으로 생성되는 전이도 §14-1의 전이표 완전성 검사 대상에 포함한다.
```

규칙 2의 예외가 핵심입니다. **"쓰기 중 취소"를 깨끗한 취소로 처리하면 불변식 9가 무력화됩니다.**

---

### 🔴 E. canonical serialization이 정의되지 않아 digest 계약이 미완성

`canonical`이라는 단어가 세 곳에 나오지만 **규범적 정의가 없습니다.**

| §4-2 | "JSON은 canonical serialization을 사용한다" |
| §5-3 | `action_digest = SHA-256(..., canonical_arguments)` |
| §9-2 | "canonical form과 Schema version을 사용한다" / "Canonical CBOR" |

digest는 **상호운용 계약**입니다. 정의가 없으면:

- 불변식 5(승인은 동일 `action_digest`에만 유효)가 구현자마다 다르게 동작하고,
- §14-3의 감사 체인 변조 탐지 테스트가 다른 빌드에서 재현되지 않으며,
- §9-2의 "외부 앵커링"이 검증 불가능해집니다.

추가로 §5-3은 **자기 모순**을 포함합니다. "단순 문자열 연결을 금지하고 length-prefix binary encoding을 사용한다"고 규정한 직후 `SHA-256("cogito-action-v1", session_id, turn_id, ...)`라는 쉼표 표기를 제시합니다. 이는 인코딩을 지정하지 않은 의사코드입니다.

**조치** — §5-3에 정확한 인코딩을 명시.

```text
canonical JSON: RFC 8785 (JCS) 를 채택한다.
  - 키 정렬: UTF-16 code unit 오름차순
  - 숫자: ECMAScript Number-to-String 알고리즘
  - 유니코드 정규화 수행 여부를 명시 (권장: 수행하지 않음, 바이트 보존)

digest 인코딩:
  H(f₁..fₙ) = SHA-256( concat( u32le(len(fᵢ)) ‖ fᵢ ) )
  - 모든 fᵢ는 UTF-8 바이트열, 도메인 태그 f₁ 포함
  - 정수 필드는 u64le 고정폭으로 인코딩 (십진 문자열 금지)
```

§9-2의 감사 체인도 동일 인코딩을 재사용하도록 통일하고, "Canonical CBOR 또는" 선택지는 제거해 구현 분기를 없앱니다.

---

### 🟠 F. 다중 Action을 `Failed`로 처리하면서, 애초에 발생시키지 않을 요구가 없다

§1-1 + §7-2(`Propose | MultipleActions | Failed`) + §5-1(`ordinal ≥ 1`이면 오류)은 일관됩니다. MVP 단순화로서 타당합니다.

문제는 **현대 모델이 기본적으로 병렬 도구 호출을 생성한다**는 점입니다. 이 규칙만 있으면 정상적인 모델 동작이 곧바로 턴 실패(`Failed`는 종료 상태)가 되어, 사용자는 아무 결과도 얻지 못하고 `StartNextTurn`부터 다시 시작해야 합니다.

**누락된 요구는 "요청 단계에서 병렬 호출을 억제하라"입니다.**

**조치** — §10-1에 추가:

```
프로바이더는 요청 시점에 단일 Action만 반환되도록 강제해야 한다.
  - OpenAI 호환 HTTP : parallel_tool_calls = false 를 전송하고,
                       서버가 무시할 수 있음을 가정해 응답도 검사한다.
  - llama.cpp        : 문법이 단일 tool_call 객체만 허용하도록 생성한다.
억제를 적용했음에도 다중 Action이 반환되면 Failed로 처리하고 provider_id·
model digest와 함께 감사한다 (프로바이더 규격 위반의 증거로 사용).
```

이렇게 하면 `Failed` 경로는 "정상 상황"이 아니라 "프로바이더 이상 신호"가 되어 의미가 명확해집니다.

---

### 🟠 G. 승인 재사용·재진입 가드가 규범으로 없다

§5-3은 **Permit**의 단일 사용을 규정합니다("한 번 사용하면 폐기"). 그러나 **ApprovalRecord**에 대해서는 §5-4가 상태(`Approved`/`Rejected`/`Expired`/`Cancelled`)와 nonce만 정의하고, 소비(consume) 규칙이 없습니다.

§14-2가 "재사용 승인 시 write 0회"를 테스트로 요구하지만, **통과시킬 규범 조항이 §5·§8에 없습니다.** 테스트가 검증할 대상이 정의되지 않은 상태입니다.

또한 §7-2의 `AwaitApproval → Gate` 재진입에 횟수 제한이 없어, Gate가 다시 `Ask`를 반환하면 `Gate → AwaitApproval → Gate → ...` 순환이 가능합니다. §8-1 7단계에서 기존 ApprovalRecord를 발견해 통과시키는 것이 의도지만, 명시되지 않았습니다.

**조치** — §5-4에 추가:

```
- ApprovalRecord는 단일 사용이다. Permit 발급 시 Consumed로 전이하며
  재사용 시도는 Deny(reason_code=approval_already_consumed)로 감사한다.
- 하나의 action_id에 대한 Gate 재진입은 최대 1회로 제한한다.
  2회째 Ask 판정은 Deny로 강등하고 감사한다.
- 승인은 (action_digest, permit_scope_digest, session_id, turn_id) 4개
  전부가 일치할 때만 유효하다. 하나라도 다르면 새 승인을 요구한다.
```

---

### 🟠 H. "제한된 `pattern`"의 제한 내용이 정의되지 않음 — ReDoS 경로

§6은 `제한된 pattern`을 허용 키워드로 두지만 제한의 내용이 없습니다. §14-4는 퍼징을 요구하지만 **통과 기준(정규식 실행시간 상한)이 없습니다.**

실질적 위험: `json-schema-validator`는 기본적으로 `std::regex`를 사용하며, 이는 백트래킹 엔진입니다. 도구 매니페스트가 신뢰 경계 안에 있더라도 §10-6의 MCP 원격 스키마 변환 경로를 통해 외부에서 유입될 수 있고, §3 불변식 10이 바로 그것을 "신뢰하지 않는다"고 규정합니다.

**조치** — §6에 다음 중 하나를 명시하고 §14-4에 통과 기준 추가.

```
(a) 문법 제한 : ^ 로 시작하고 $ 로 끝나야 하며, 문자클래스와
                한정 반복 {n,m} 만 허용. 중첩 반복(( … )+)+ 금지.
                패턴 길이 상한과 최대 반복 횟수 상한을 둔다.   ← GBNF 앵커 요구와도 일치
(b) 엔진 교체 : RE2 등 선형시간 보장 엔진으로 컴파일한다.
공통       : 부팅 시 패턴 복잡도 검사에 실패하면 프로세스 시작을 실패시킨다.
             §14-4 통과 기준 = 최악 입력에서도 패턴당 실행시간 < N ms.
```

(a)를 택하면 §3-A의 Tier-G 정합성까지 동시에 해결됩니다.

---

### 🟠 I. `turn_end` 기록 실패 후 복구 경로가 없어 인스턴스가 영구히 잠긴다

| 불변식 12 | "기록 실패는 사용자에게 반환하고 **다음 턴을 막는다**" |
| §7-2 | "`StartNextTurn`은 직전 `turn_end`가 **성공한 뒤에만** 허용" |

두 조항의 결합으로, `turn_end` 커밋이 한 번 실패하면 (디스크 가득참, 일시적 잠금 등) **해당 Agent 인스턴스는 복구 수단 없이 영구히 사용 불가**가 됩니다. 재시도 API도, 세션 봉인 API도 정의되어 있지 않습니다.

이는 fail-closed 설계로서 안전한 방향이지만, **운영 가능한 제품이 되려면 명시적 탈출구가 필요**합니다. §9-1이 이미 `audit_recovery` 이벤트 종류를 정의했으므로 여기에 연결하면 됩니다.

**조치** — §11에 API 추가, §3 불변식 12에 단서 추가:

```
cogito_retry_finalize(agent)   : turn_end 재기록을 시도한다. 성공 시 StartNextTurn 해제.
cogito_seal_session(agent)     : 복구 불가 판정 시 audit_recovery 이벤트로 세션을 봉인하고
                                 해당 인스턴스를 Failed-terminal로 확정한다.
                                 봉인 자체도 기록에 실패하면 프로세스를 중단한다.
```

---

### 🟠 J. `indeterminate` 이후의 처리 절차가 정의되지 않음

`indeterminate`는 불변식 9, §5-5, §10-4, §14-3에 걸쳐 등장하는 **이 설계의 가장 중요한 신규 상태**입니다. 그런데:

- 대화(`ConversationStore`)에 무엇을 주입하는가?
- 같은 턴을 계속 진행할 수 있는가, 즉시 `Done`인가?
- 동일 도구·동일 인자에 대한 후속 Action을 허용하는가?
- 운영자에게 무엇을 요구하며, 해소는 어떻게 기록되는가?

어느 것도 규정되지 않았습니다. §10-4는 "확인 실패는 성공이 아니라 `indeterminate`다"까지만 말하고 멈춥니다. 불변식 9의 "자동 재시도하지 않는다"만으로는 **LLM이 다음 턴에 같은 쓰기를 다시 제안하는 것**을 막지 못합니다 (그것은 자동 재시도가 아니라 새 제안이므로).

**조치** — §3 또는 §10-4에 추가:

```
indeterminate 발생 시:
  1. 대화에는 구조화된 { status: "indeterminate", tool, node_id, before,
     requested, after: null } 만 주입한다. 성공/실패로 서술하지 않는다.
  2. 동일 (tool_name, canonical_arguments) 조합은 해당 세션에서
     자동으로 Ask 이상으로 강등한다. Policy Allow로 낮출 수 없다.
  3. 운영자 확인 이벤트(actor_type=human)가 감사에 기록되기 전까지
     해당 설비 대상 write 도구 전체를 Ask로 강등하는 것을 기본값으로 한다.
  4. 턴은 계속 진행 가능하되, 종료 시 outcome에 indeterminate를 표시한다.
```

---

### 🟠 K. `effect`와 `risk`의 최소 하한이 정의되지 않음

§5-2가 `effect`(none/write/destructive)와 `risk`(low/medium/high/critical)를 **별도 축**으로 분리한 것은 좋은 개선입니다. 그러나 §10-4는 이렇게 씁니다.

> `access=write`인데 effect가 `none`이거나 **위험도가 기준보다 낮으면** 시작 실패한다.

**"기준"이 어디에도 정의되어 있지 않습니다.** 시작 실패 조건이므로 구현자가 임의로 정할 수 없는 값입니다.

**조치** — §5-2에 하한 표를 명시:

| `effect` | 최소 `risk` | `approval_required` |
| --- | --- | --- |
| `none` | `low` | 선택 |
| `write` | `medium` | 기본 `true` (명시적 정책으로만 완화, 감사) |
| `destructive` | `high` | **`true` 강제, 완화 불가** |

부팅 시 이 하한을 위반하는 ToolDescriptor가 있으면 프로세스 시작을 실패시킵니다(§6의 스키마 컴파일 실패와 동일 정책).

---

### 🟡 L. MCP `server/discover`의 위상이 규격보다 약하게 서술됨

§10-6은 이렇게 씁니다.

> 최소 범위는 `tools/list`, `tools/call`; **capability 확인이 필요하면** `server/discover`를 사용한다.

규격 원문은 더 강합니다.

- 서버는 `server/discover`를 **MUST 구현**한다.
- stdio에서 modern 클라이언트는 legacy 서버와 **결정론적으로 실패하기 위해 SHOULD 먼저 호출**한다.

§10-6이 규정한 Cogito++의 MCP 전송은 **stdio subprocess가 기본**이므로, `server/discover`는 "필요하면"이 아니라 **기본 프로브**여야 합니다.

또한 modern 클라이언트의 핵심 의무인 **`UnsupportedProtocolVersionError`(-32022) 수신 시 `supported` 목록에서 재선택 후 재시도**가 §10-6에 없습니다. 다만 Cogito++는 부팅 시 Registry를 동결하므로, 다음처럼 좁히는 것이 불변식과 더 잘 맞습니다.

**조치**

```
- stdio 연결 시 server/discover 를 최초 프로브로 반드시 호출한다.
- UnsupportedProtocolVersionError(-32022) 재협상은 부팅 시 1회만 허용한다.
  런타임 중 재협상은 금지하고 어댑터를 오류 상태로 전환한다
  (Registry 동결 이후 도구 카탈로그가 바뀌면 안 되므로).
- 협상 결과 프로토콜 버전을 Tool catalog snapshot digest에 포함한다.
```

---

### 🟡 M. `§14-6` 골든 리플레이 키에 3개 항목이 빠져 있다

현재 키:

```
user input + FakeProvider script/version + FakeTool fixture/version
+ FakeClock event sequence + policy/config/registry digest + event queue order
```

누락:

| 누락 항목 | 근거 |
| --- | --- |
| **ContextCompactor 버전** | §4-2가 "compactor 버전을 감사"하라고 요구. 축약이 발생하는 긴 턴은 compactor가 바뀌면 프롬프트가 달라지고 골든이 깨진다 |
| **prompt / chat template 버전** | §10-1이 "chat template digest를 감사"하라고 요구 |
| **Tool Schema export 정렬 규칙 버전** | §4-2가 "이름 기준 고정 정렬 + canonical serialization"을 요구. 정렬 규칙이 바뀌면 프롬프트가 달라진다 |

세 항목 모두 문서의 다른 절이 이미 "감사하라"고 요구하는 값이므로, **골든 키에도 포함하는 것이 일관됩니다.**

---

### 🟡 N. 비밀정보의 보관 위치가 정의되지 않음

§10-3은 금지만 규정합니다.

> API key와 client key는 설정 JSON이나 감사 로그에 평문으로 두지 않는다.

**어디에 두어야 하는지가 없습니다.** §13의 설정 산출물 목록(`cogito.json` 등)에도 secret 취급 규칙이 없고, §16은 "credential 운영 방식"을 외부 결정으로 넘겼습니다. 그런데 §15 Phase 2가 "HTTP 프로바이더 통과"를 완료 조건으로 두므로, 그 전에 결정되어야 합니다.

**조치** — §13에 최소한의 참조 방식을 규정 (구체적 저장소 선택은 §16에 남기더라도).

```
config 파일은 비밀값 자체가 아니라 참조만 담는다.
  "api_key_ref": "env:COGITO_LLM_API_KEY"      | "file:/run/secrets/llm.key"
                 "wincred:cogito/llm"          | "keyring:cogito/llm"
코어는 참조를 해석해 메모리에서만 사용하고, 로그·감사·오류 메시지·
크래시 덤프에 값이 노출되지 않도록 전용 SecretString 타입으로 취급한다.
파일 참조는 권한(0600 / ACL)을 부팅 시 검사하고 위반 시 시작 실패한다.
```

---

### 🟡 O. `monotonic_ns`의 프로세스·부팅 경계가 정의되지 않음

§9-1은 `monotonic_ns INTEGER NOT NULL`을 두고, §14-3은 **"UTC 역행에도 monotonic 순서 보존"** 테스트를 요구합니다.

그러나 monotonic clock은 **프로세스 재시작과 재부팅에서 리셋**됩니다. 재시작 후 첫 이벤트의 `monotonic_ns`는 이전 행보다 작을 수 있으므로, 이 테스트는 **단일 프로세스 수명 내에서만** 의미가 있습니다. 문서에 그 단서가 없어, 구현자가 전역 단조성을 가정하면 §14-3이 간헐적으로 실패합니다.

**조치** — `process_epoch_id`(프로세스 시작 시 생성하는 UUID 또는 부팅 후 카운터) 컬럼을 추가하고, 순서 판정을 명시:

```
전역 순서 권위    : seq (AUTOINCREMENT — 재사용되지 않음)
프로세스 내 순서  : (process_epoch_id, monotonic_ns) 사전식
표시·상관관계     : wall_time_utc (순서 판정에 사용하지 않음)
§14-3 테스트는 동일 process_epoch_id 범위 내에서 monotonic 단조성을 검사한다.
```

---

### 🟡 P. `idempotency_key`의 도출 방식이 정의되지 않음

`idempotency key`는 §9-3(감사 기록), §10-7(MQTT write)에 등장하고 §5-2에 `idempotency` 분류 축이 있지만, **무엇으로부터 도출하는지가 없습니다.**

불변식 9가 자동 재시도를 금지하므로 시도(attempt)별 키는 불필요하고, `action_digest`로 충분합니다.

**조치** — §5-3에 한 줄 추가.

```
idempotency_key = lowercase_hex(action_digest)
외부 시스템(MQTT/OPC UA/HTTP)에 전달할 때의 인코딩과 필드명을
어댑터별로 §10에 명시한다.
```

---

### 🟡 Q. 기획안으로 역전파해야 할 변경이 `§1-2`에 누락됨

이 문서는 §7 서두에서 "원문과 충돌할 경우 이 문서를 우선 적용한다"고 선언했지만, **`Cogito++_기획안.md`는 발표·심사용 문서로 별도 유통**됩니다. 다음 두 항목은 기획안 본문과 직접 충돌하는데 §1-2의 정정 표에 없습니다.

| 기획안 위치 | 기획안 서술 | 이 문서의 결정 |
| --- | --- | --- |
| 3-4 #3 PermissionPolicy | "`Allow / Ask / Deny / **AlwaysAllow**` 판정" | §5-2: **`AlwaysAllow`를 런타임 판정값으로 두지 않는다** |
| 3-8 로드맵 ④ | "ESP32 센서 연동 / 경량 프로파일" | §2 제외 범위: **ESP32에서 전체 코어 실행 보류**, MQTT/직렬 센서 노드로만 연동 |
| 3-7 기술 스택 | "대상 플랫폼 x86-64 → ARM64 → **ESP32**" | §1-1: 동일 취지로 축소 |

**조치** — §1-2 표에 3행을 추가하거나, 별도로 "기획안 수정 요청 항목" 절을 두어 기획 담당자가 반영할 수 있게 합니다. 심사 발표에서 기획안과 구현 기준서가 다른 말을 하면 신뢰도 손실이 큽니다.

---

## 4. 조치 우선순위

### 코드 작성 전 반드시 (🔴 5건 — 전부 문서 수정으로 해결)

| # | 항목 | 조치 위치 | 예상 작업 |
| --- | --- | --- | --- |
| A | Tier-G 부분집합 + `grammar_coverage` 도입 | §6, §10-2, §14 | 표 1개 + 규칙 4줄 + 테스트 1항목 |
| B | SHA-256을 코어 의존성으로 이동 | §1-1, §4-1 | 2줄 수정 |
| C | 승인 재평가를 FSM 재진입으로 일원화 + verdict 커밋 시점 명시 | §7-2, §8-1 | 8단계 삭제, 커밋 규칙 3줄 추가 |
| D | `Cancel`/`AuditError` 전이 규칙 3개 추가 | §7-2 | 규칙 3줄 |
| E | canonical JSON(RFC 8785) + digest 인코딩 확정 | §5-3, §9-2 | 규격 블록 1개 |

이 5건은 **모두 설계 변경이 아니라 미완성 서술의 완성**입니다. 아키텍처를 되돌릴 필요는 없습니다.

### Phase 1 착수와 병행 (🟠 6건)

F(병렬 호출 억제) · G(승인 소비 규칙) · H(pattern 제한) · I(`turn_end` 복구 API) · J(`indeterminate` 절차) · K(effect↔risk 하한)

G·H·K는 §14-2/§14-4의 테스트가 이미 이들을 검증 대상으로 삼고 있으므로, **테스트를 작성하려면 먼저 규범이 필요합니다.** Phase 1 안에서 해소하는 것이 자연스럽습니다.

### Phase 2 이전 (🟡 6건)

L(MCP `server/discover`) · M(골든 키 3종) · N(비밀 참조 방식) · O(`process_epoch_id`) · P(`idempotency_key`) · Q(기획안 역전파)

N은 Phase 2의 HTTP 프로바이더 완료 조건에 걸리므로 그 전에 확정해야 합니다. Q는 발표 일정에 맞춰 별도 처리.

---

## 5. 유지해야 할 결정

검증 과정에서 특히 잘 설계되었다고 판단한 항목입니다. 후속 논의에서 되돌리지 않기를 권합니다.

| 항목 | 이유 |
| --- | --- |
| **`ExecutionPermit` 도입** | `Verdict`를 Invoker에 넘기지 않고 코어만 만들 수 있는 단명 토큰을 넘기는 구조가 불변식 1(단일 실행 경로)을 **타입 수준에서** 강제합니다. 원문 스켈레톤의 가장 큰 구조적 약점을 정확히 메웠습니다 |
| **`effect`와 `risk`의 축 분리** | "위험도가 높다"와 "부수효과가 있다"는 독립 축입니다. 읽기인데 민감한 도구(생산 원가 조회)와 쓰기인데 저위험인 도구(라벨 문구 변경)를 같은 축에 두면 정책이 반드시 왜곡됩니다 |
| **금지 노드의 tombstone 유지** | 원문 결함 7번의 정확한 처방이며, "검토 후 금지했다"와 "등록을 빠뜨렸다"를 감사에서 구분 가능하게 합니다 |
| **`AlwaysAllow` 제거** | 런타임 상태로서의 영구 허용은 감사 가능성과 양립하지 않습니다. 고우선 정책 규칙으로 옮긴 것이 옳습니다 |
| **비동기 승인 (`PENDING_APPROVAL` → `resume_turn`)** | 원문의 `cogito_approve_fn` 블로킹 콜백은 HMI UI 스레드를 잠급니다. C ABI 설계 오류를 정확히 잡았습니다 |
| **`indeterminate` 상태 신설** | 산업 제어에서 "성공도 실패도 아님"은 실재하는 상태입니다. 이를 명시적 상태로 만든 것은 원문보다 명백히 진전입니다 (단 §3-J의 후속 절차 필요) |
| **골든 리플레이 조건의 명시화** | "같은 입력 → 같은 경로"라는 마케팅 문구를 검증 가능한 키 묶음으로 바꾼 것이 이 문서의 가장 큰 기여입니다 |
| **fail-closed 감사 (불변식 7·8)** | "감사 기록에 실패하면 실행하지 않는다"는 산업용 에이전트에서 타협 불가한 원칙이며, 테스트(§14-2)까지 연결되어 있습니다 |

---

## 6. 최종 판정

**이 문서는 구현 기준서로 채택할 수 있습니다.** 사실관계에 오류가 없고, 원문 결함 지적에 오탐이 없으며, 안전 관련 핵심 결정(Permit, tombstone, fail-closed 감사, 비동기 승인, indeterminate)은 모두 올바른 방향입니다.

다만 **현재 상태로 코드를 작성하면 §3의 🔴 5건에서 구현자가 임의 결정을 내리게 되고, 그 결정은 감사·승인·재현성 계약에 직접 영향을 줍니다.** 특히 §3-E(canonical/digest)와 §3-C(승인 재평가)는 나중에 바꾸면 기존 감사 데이터가 무효화되는 종류의 결정입니다.

권고 순서:

1. 🔴 5건을 반영한 **v0.2**를 만든다 (문서 작업 1일 내외, 설계 변경 없음)
2. §15 Phase 0의 ADR 8건을 승인한다 — 이때 🟠 6건을 각 ADR 안에서 해소한다
   - `0001-fsm-turn-and-action` → §3-D, §3-F
   - `0003-approval-and-identity` → §3-C, §3-G
   - `0004-audit-integrity-and-failure` → §3-E, §3-I, §3-O
   - `0005-timeout-retry-idempotency` → §3-J, §3-P
   - `0006-schema-dialect` → §3-A, §3-H
3. P0 코드 착수

---

## 부록. 검증에 사용한 1차 자료

이 보고서의 §1 판정은 전부 아래 자료를 **직접 조회**해 확인했습니다. 문서의 §18 목록과 대조 가능합니다.

| 확인 항목 | 출처 |
| --- | --- |
| MCP 핸드셰이크 제거, modern/legacy 구분, `server/discover` MUST | [MCP 2026-07-28 Versioning and Compatibility](https://modelcontextprotocol.io/specification/2026-07-28/basic/lifecycle) |
| MCP stateless 코어, per-request capability negotiation | [MCP 2026-07-28 Specification](https://modelcontextprotocol.io/specification/2026-07-28) |
| GBNF 미지원 키워드 (`number` 범위 미적용, 앵커 필수 등) | [llama.cpp grammars/README.md](https://github.com/ggml-org/llama.cpp/blob/master/grammars/README.md) |
| `JSON_NOEXCEPTION` → `std::abort()` | [nlohmann/json JSON_NOEXCEPTION](https://json.nlohmann.me/api/macros/json_noexception/) |
| vcpkg 포트명 `json-schema-validator` (v2.4.0) | [vcpkg ports/json-schema-validator](https://github.com/microsoft/vcpkg/blob/master/ports/json-schema-validator/vcpkg.json) |
| CMake 타깃 `nlohmann_json_schema_validator::validator` | [pboettch/json-schema-validator CMakeLists.txt](https://github.com/pboettch/json-schema-validator/blob/main/CMakeLists.txt) |
| open62541 포트 feature 목록 (v1.4.14) | [vcpkg ports/open62541](https://github.com/microsoft/vcpkg/blob/master/ports/open62541/vcpkg.json) |
| CRA 보고 의무 2026-09-11 | [EU CRA reporting](https://digital-strategy.ec.europa.eu/en/policies/cra-reporting) |
| CRA 발효 2024-12-10, 주요 의무 2027-12-11 | [EU Cyber Resilience Act](https://digital-strategy.ec.europa.eu/en/policies/cyber-resilience-act) |

---

*본 보고서는 `Cogito++_구현_요구사항.md`를 검증 대상으로 하며, 검증자는 그 입력 문서 중 하나인 `Cogito++_OSS_기술스택_아키텍처.md`의 작성자다. §1·§2에서 확인된 원문 오류는 해당 아키텍처 문서의 수정 대상이다.*
