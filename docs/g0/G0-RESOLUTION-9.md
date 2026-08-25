# G0 자기모순 9건 정정 보고서

| | |
| --- | --- |
| **대상** | G0-01 · G0-05 · G0-09 · G0-23 · G0-24 · G0-25 · G0-26 · G0-29 · G0-31 |
| **작성** | Claude (계약 관리자 · 감사관), Gemini (정합화) |
| **기준일** | 2026-08-21 (2026-08-24 일부 승인) |
| **원본** | `Cogito++_구현명세서.md` v1.0, `Cogito++_개발_작업체크리스트.md` §2-2 |
| **상태** | **Partially Accepted — G0-05 · G0-23 · G0-26 (2026-08-24), G0-25 · G0-29 (2026-08-25 S2) 사람 승인 완료.** 나머지 4건(①③⑤⑨)은 사람 승인 대기 |
| **관련 ADR** | `0001-fsm-turn-and-action`(Proposed), `0004-audit-integrity-and-failure`(Accepted) |

> **읽는 법** — 각 항목은 `[모순] → [선택지] → [확정] → [명세 수정 diff] → [영향]` 순이다.
> G0-05, G0-23, G0-26 (2026-08-24) 및 G0-25, G0-29 (2026-08-25)는 사람 승인으로 **Accepted** 되었다.

---

## 0. 요약

| # | 항목 | 확정 결정 | 상태 | 되돌림 |
| --- | --- | --- | --- | --- |
| ① | G0-01 | ABI **v1.1 단일 기준**. `MAJOR=1 MINOR=1`. §8-2 의 v1.0 시그니처는 명세에서 삭제 | Proposed | 가능 |
| ② | G0-05 | `operation_digest` 신설(`cogito-operation-v1`). 잠금 키와 멱등 키를 **분리** | **Accepted** | **불가** |
| ③ | G0-09 | 무값 성공은 `Error` 로 통일. `Result<void>` 특수화는 제네릭 전용. 예외 정책 확정 | Proposed | S0/S1/S2 한정 승인 |
| ④ | G0-23 | **골든표가 권위.** 서술 규칙을 C99 `%g`(지수 최소 2자리)로 정정. **+ 골든표 자체의 오류 1행 발견·정정** | **Accepted** | **불가** |
| ⑤ | G0-24 | 보편 규칙 **R0~R4** 완전 정의. `Idle` 은 R1/R2 대상 아님 | Proposed | 가능 |
| ⑥ | G0-25 | `invoker.hpp` · `ops_log.hpp` · `context_compactor.hpp` · `action.hpp` · `budget.hpp` 계약 확정 | **Accepted** | **S2 한정 승인** |
| ⑦ | G0-26 | 도메인 태그 **9개**(기존 8 + `kOperation`) + projection 표 확정 | **Accepted** | **불가** |
| ⑧ | G0-29 | 모드를 숫자 비교하지 않는다. **effect 상한으로 사상 후 최솟값** | **Accepted** | **S2 한정 승인** |
| ⑨ | G0-31 | 증가 시점 = `AwaitApproval→Gate`(`Event::Approved`). 상한 1. reset 시점 확정 | Proposed | 가능 |

**핵심 결정들이 승인되어 S0, S1 및 S2 착수가 허가되었다.**

---

## ① G0-01 — C ABI 버전 표기 일관성

### 모순

| 위치 | 서술 |
| --- | --- |
| §8-2 | `#define COGITO_ABI_VERSION_MAJOR 1` / `#define COGITO_ABI_VERSION_MINOR 0` |
| §8-4 | "이 절은 v1.0 을 대체하며 `COGITO_ABI_VERSION_MINOR = 1` 이다" |
| §8-2 | `cogito_run_turn(agent, in, out)` — 주체 인자 없음 |
| §8-4 [S-1] | `cogito_run_turn(agent, subj, in, out)` — 주체 필수 |
| §8-3 | 예외 래퍼 **예시가 이미 `subj` 를 받는다** — 즉 v1.0 을 스스로 위반 |

같은 헤더에 두 개의 `cogito_run_turn` 이 있다. 구현자는 어느 쪽을 만들지 알 수 없다.

### 확정

**제품 최초 구현 기준은 ABI v1.1 하나다.** v1.0 은 존재한 적 없는 것으로 취급한다
(릴리스된 바이너리가 없으므로 호환 계층이 필요 없다).

```c
#define COGITO_ABI_VERSION_MAJOR 1
#define COGITO_ABI_VERSION_MINOR 1
/* cogito_abi_version() 반환값 = (MAJOR << 16) | MINOR */
```

**버전 정책 (이 규칙 자체가 계약이다)**

| 증가 | 조건 |
| --- | --- |
| **MAJOR** | 기존 함수의 시그니처·의미 변경, 함수 삭제, 구조체 기존 필드의 순서·타입·의미 변경, 상태 코드의 의미 변경, `@thread` 분류 변경 |
| **MINOR** | 함수 추가, 구조체 **끝에** 필드 추가(`struct_size` 로 보호), 상태 코드 추가, 조회 API 추가 |

**호출자 의무** — `cogito_agent_create` 는 `cfg->abi_version` 을 검사한다.

```c
uint32_t v = cogito_abi_version();
if ((v >> 16) != COGITO_ABI_VERSION_MAJOR) -> COGITO_ERR_INVALID_ARG   /* major 불일치는 거부 */
if ((v & 0xFFFF) <  COGITO_ABI_VERSION_MINOR) -> COGITO_ERR_INVALID_ARG /* 코어가 더 낮으면 거부 */
/* 코어 minor 가 더 높은 것은 허용한다(전방 호환) */
```

### 명세 수정

```diff
 ### 8-2. 헤더 (v1.0 기준선)
-> ⚠️ 아래는 CLI 단일 호출자를 전제한 v1.0이다. …
+### 8-2. 헤더 (ABI v1.1 — 유일한 규범)
+> v1.0 은 릴리스된 적이 없으므로 호환 계층을 두지 않는다. 아래가 유일한 계약이다.

-#define COGITO_ABI_VERSION_MINOR 0
+#define COGITO_ABI_VERSION_MINOR 1

-COGITO_API cogito_status_t cogito_run_turn(cogito_agent_t a, const char* user_input, char** out_json);
+COGITO_API cogito_status_t cogito_run_turn(cogito_agent_t a,
+                                           const cogito_subject_t* subj,
+                                           const char* user_input, char** out_json);
```

§8-4 의 제목을 "ABI v1.1 — 다중 호출자 호스트를 위한 확장" 에서
**"§8-2 보충 — 주체·스레드 친화성·소유권 규칙"** 으로 바꾸고,
"v1.0 을 대체한다"는 문장을 삭제한다(대체할 v1.0 이 더 이상 없다).

### 영향

- §8-2 의 모든 상태변경 함수 시그니처에 `const cogito_subject_t*` 추가
- `COGITO_ERR_WRONG_THREAD = 26` 을 §8-2 enum 에 편입
- `cogito_cancel_turn` → `cogito_request_cancel`(any) / `cogito_cancel_turn`(agent-loop-only) 분리 확정
- **Codex 인계 필요**: `src/abi/cogito_abi.cpp` 는 v1.1 만 구현한다

---

## ② G0-05 — Operation Digest 및 잠금 키

### 모순

§6-4 는 `indeterminate` 발생 시 동일 작업을 잠근다.

```cpp
indeterminate_lock_.insert(action.tool_name + "|" + action_digest.hex());
```

그런데 §6-1 의 `action_digest` 는 `session_id · turn_id · action_id` 를 포함한다.
**같은 도구·같은 인자라도 다음 턴에서는 digest 가 달라진다.**

→ 모델이 다음 턴에 같은 쓰기를 다시 제안하면 잠금이 매칭되지 않는다.
§6-4 가 만든 안전 통제가 **한 턴만 살고 사라진다.**

### 선택지

| # | 방식 | 문제 |
| --- | --- | --- |
| 1 | 잠금 키를 `tool_name + CCJ(arguments)` 원문으로 | 키가 무한정 길어지고 감사 payload 에 인자 원문이 남는다(§7-5 위반) |
| 2 | `action_digest` 에서 session/turn/action 을 제거 | 승인 결합(불변식 5)이 깨진다. action_digest 는 턴 고유해야 한다 |
| 3 | **별도 `operation_digest` 를 신설** | 도메인 태그가 하나 늘어난다 |

### 확정 — 선택지 3. **되돌림 불가**

```
domain::kOperation = "cogito-operation-v1"

operation_digest = SHA-256( LP("cogito-operation-v1") || LP(tool_name) || LP(CCJ(arguments)) )
```

**두 digest 의 목적이 다르다는 것을 명문화한다.**

| digest | 포함 | 수명 | 용도 |
| --- | --- | --- | --- |
| `action_digest` | domain·session·turn·action_id·tool·CCJ(args) | 한 Action | 승인 결합(불변식 5), `idempotency_key` |
| `operation_digest` | domain·tool·CCJ(args) | 세션 전체 | `indeterminate` 잠금, 동일 작업 반복 판정 |

- **`idempotency_key = lowercase_hex(action_digest)` 는 그대로 둔다.**
  불변식 9 가 자동 재시도를 금지하므로, 다음 턴의 재제안은 *새 작업*이고 새 키를 받아야 한다.
  외부 시스템(MQTT/OPC UA)에 같은 키를 주면 정당한 재조작이 중복으로 판정돼 무시된다.
- **잠금 키는 `operation_digest`.** 턴을 넘어 유지되어야 안전 통제가 성립한다.

### 명세 수정

```diff
 // §6-4 indeterminate 처리
-  indeterminate_lock_.insert(action.tool_name + "|" + action_digest.hex());
+  indeterminate_lock_.insert(operation_digest.hex());   // 턴을 넘어 유지된다

 // §4-12 AgentLoop
-  std::set<std::string> indeterminate_lock_;
+  std::set<std::string> indeterminate_lock_;   // operation_digest.hex() 집합. 세션 수명.
```

§6-1 에 `ComputeOperationDigest` 를 추가하고, §3-4 `reason_code` 의
`indeterminate_lockdown` 설명에 "operation_digest 기준" 을 명시한다.

### 영향

- `include/cogito/digest.hpp` 에 `domain::kOperation` · `ComputeOperationDigest` 추가
- `GateInput` 에 `bool indeterminate_locked` 를 계산할 때 `operation_digest` 를 쓴다
- **테스트 추가**: "다른 turn 에서도 잠금이 유지된다" (검증보고서가 요구한 종료 증거)
- 고정 벡터 필요: `operation_digest` 는 §10-2 `canonical/digest_vectors` 에 편입

---

## ③ G0-09 — `Result<void>` 와 예외 정책

### 모순

§4-1 은 `Result<T>` 를 정의하지만 `T = void` 를 다루지 않는다.
그런데 §4-5·§4-10·§6-3 의 공개 API 는 이미 무값 성공을 `Error` 로 반환한다
(`Error Register(...)`, `Error Dispatch(...)`, `Error RetryFinalize()`).

즉 **관례는 이미 `Error` 인데 규범이 없다.** 제네릭 코드에서 `Result<void>` 를 쓰면 컴파일되지 않는다.
또한 "내부 예외 허용 범위"와 OOM 정책이 §1-1 한 줄 외에 없다.

### 확정

**규칙 1 — 직접 선언은 `Error` 로 통일한다.**

```cpp
[[nodiscard]] Error Freeze();                 // ✅ 무값 성공
[[nodiscard]] Result<Digest> Compute(...);    // ✅ 값 있는 성공
[[nodiscard]] Result<void> Foo();             // ❌ 공개 API 에 쓰지 않는다
```

**규칙 2 — `Result<void>` 명시적 특수화를 제공하되, 템플릿 매개변수로 `void` 가 올 수 있는
제네릭 코드에서만 쓴다.** `Error` 와 상호 변환된다.

**규칙 3 — 예외 정책**

| 범위 | 규칙 |
| --- | --- |
| 코어 내부 | 예외 사용 허용. `-fno-exceptions` 빌드는 지원하지 않는다(요구사항 §1-2 확정 사항) |
| C ABI 경계 | 예외가 넘지 않는다. §8-3 가드가 `bad_alloc` → `COGITO_ERR_INTERNAL`, 그 외 → `COGITO_ERR_INTERNAL` |
| 도구 핸들러 | `ToolInvoker::Invoke` 가 **자체적으로** 잡아 `ToolResult{status=Error}` 로 변환한다. 최외곽 가드에만 의존하면 §6-2-a 의 `tool_result` 커밋을 건너뛰고 FSM 이 `Execute` 에 남는다 |
| OOM | 감사 커밋 경로의 OOM 은 **fail-closed**. `Errc::AuditWriteFailed` 로 처리하고 write 를 실행하지 않는다. 조용히 계속하지 않는다 |
| `noexcept` | 소멸자 · 이동 생성자/대입 · 관측자(getter) · `Fsm::current()` · `ExecutionPermit` 접근자에만 붙인다. 나머지는 붙이지 않는다 |

**규칙 4 — 잘못된 접근은 정의된 동작을 갖는다.** 실패 `Result` 에서 `value()` 를 호출하면
`assert` 에 의존하지 않고 **Release 에서도** `std::terminate` 로 중단한다.
조용한 UB 를 남기지 않는다(체크리스트 S1-01 요구).

### 명세 수정

§4-1 `result.hpp` 에 `Result<void>` 특수화와 위 규칙 4개를 주석으로 명문화한다.
전문은 `include/cogito/result.hpp` 참조.

---

## ④ G0-23 — CCJ 지수 표기 (+ 골든표 자체의 오류)

### 모순 (보고된 것)

| 위치 | 서술 |
| --- | --- |
| §3-1 CCJ-5 | "지수 표기는 소문자 `e`, 부호 필수, **지수 자릿수의 선행 0 제거**. (예: `1.5e-7`)" |
| §3-1-a 골든표 | `1e-7` → **`1e-07`** |

### 실측 판정

CCJ-5 규칙을 그대로 구현해 골든 숫자 벡터 11행에 돌렸다.

```
1e-7   :  %.15g -> "1e-07"     왕복 일치 -> 채택
1.5e300:  %.15g -> "1.5e+300"  왕복 일치 -> 채택
```

**C 표준 fprintf 규격(C99 §7.19.6.1 / C11·C17 §7.21.6.1) 은 `%g`/`%e` 의 지수를 "항상 최소 두 자리, 필요하면 그 이상"으로 규정한다.**
즉 `1e-7` 은 `%g` 가 만들어낼 수 없는 형태이며, 얻으려면 **별도 후처리**가 필요하다.
"선행 0 제거"는 ECMAScript/RFC 8785 의 규칙이지 C 의 규칙이 아니다.

→ **골든표가 권위다. 서술 규칙이 오류다.**

### 추가 발견 — 골든표 자체에도 오류가 1행 있다

G0-23 이 보고하지 않은 모순이다. 같은 실측에서 나왔다.

| 입력 | 골든표 기재 | 규칙이 실제로 만드는 값 |
| --- | --- | --- |
| `9007199254740994.0` (2^53+2) | `9.007199254740994e+15` | **`9007199254740994`** |

이유: `|v| ≤ 2^53` 이 아니므로 정수 fast-path 를 타지 않고 `%g` 경로로 간다.
`%.15g` 는 왕복 실패, `%.16g` 가 왕복 성공한다. 그런데 C99 의 `%g` 는
**`-4 ≤ X < P` 이면 `%f` 스타일**을 쓴다. 여기서 X(지수)=15, P(정밀도)=16 이므로
지수형이 아니라 소수점 없는 정수 표기가 나온다.

```
%.15g -> 9.00719925474099e+15    왕복 X
%.16g -> 9007199254740994        왕복 O   <- 채택
```

**골든표의 `9.007199254740994e+15` 는 어떤 정밀도로도 생성되지 않는다.**

### 확정 — **되돌림 불가**

**1. 서술 규칙 정정**

```diff
-        - 지수 표기는 소문자 'e', 부호 필수, 지수 자릿수의 선행 0 제거. (예: 1.5e-7)
+        - 지수 표기는 C99 %g 규칙을 그대로 따른다: 소문자 'e', 부호 필수,
+          지수는 최소 두 자리이며 필요하면 그 이상. (예: 1e-07, 1.5e+300)
+          후처리로 선행 0 을 제거하지 않는다.
+        - %g 는 -4 <= X < P 일 때 %f 스타일을 쓴다. 따라서 2^53 을 넘는 정수도
+          정밀도에 따라 지수형이 아닌 정수 표기가 될 수 있다. 이는 의도된 동작이다.
```

**2. 골든표 정정 (1행)**

```diff
-| `9007199254740994.0` (2^53+2) | `9.007199254740994e+15` |
+| `9007199254740994.0` (2^53+2) | `9007199254740994` |
```

**3. 구현 규범 신설 — locale 독립성 (필수)**

`printf("%g")` 는 **로케일 의존적**이다. 한국어/독일어 로케일에서 소수점이 `,` 가 되면
digest 가 전부 달라진다. 전역 로케일은 스레드 간 공유되므로 `setlocale` 로는 안전하지 않다.

```
[CCJ-5-impl] 숫자 직렬화는 로케일에 의존해서는 안 된다. 다음 중 하나를 쓴다.
  (a) std::to_chars(first, last, v, std::chars_format::general, P)   ← 권장. 정의상 로케일 독립
  (b) 로케일을 명시 고정한 snprintf:
      POSIX   — newlocale/uselocale 또는 snprintf_l
      MSVC    — _create_locale(LC_NUMERIC, "C") + _snprintf_s_l
  전역 setlocale() 에 의존하는 구현을 금지한다.
```

> ⚠️ **(a)와 (b)가 동일 바이트를 내는지는 아직 검증되지 않았다.**
> `std::to_chars(general)` 의 지수 자릿수 규약이 `printf %g` 와 동일한지 표준 문언만으로
> 단정할 수 없다. **S1 Exit Gate 의 3-컴파일러 바이트 비교로 판정한다.**
> 불일치하면 (b)를 규범으로 고정하고 (a)를 금지한다. 이 판정 전에는 어느 쪽도 "확정"이라 쓰지 않는다.

**4. MSVC 지수 자릿수 이력**

VS2015 이전 MSVC 는 지수를 **세 자리**로 출력했다(`1e-007`). 체크리스트 S0-04 의
최소 컴파일러(MSVC 2019 16.11+)는 C99 준수이므로 문제없으나,
`SelfTest()` 가 이 회귀를 잡는 것이 존재 이유 중 하나임을 명시한다.

### 검증 근거 (실행함)

```
① 정정된 골든 숫자 벡터 11행  ->  11/11 일치
② 무작위 double 200,000회 왕복 속성 검사  ->  왕복 실패 0
③ 정수 fast-path vs 순수 %g 비교  ->  1e15 에서 갈림
   (fast-path: "1000000000000000", %g: "1e+15")
   => fast-path 는 최적화가 아니라 규범이다. 2^53 경계는 의미가 있다.
```

③ 이 중요하다. **정수 fast-path 를 "어차피 %g 가 같은 결과를 낸다"는 이유로 제거하면
`1e15` 의 출력이 바뀌고 모든 digest 가 달라진다.** 명세에 이 근거를 남긴다.

### 영향

- §3-1 CCJ-5 · §3-1-a 골든표 수정
- `SelfTest()` 벡터 24개 중 1개 값 변경
- **Codex 인계 필요**: `src/canonical_json.cpp` 는 위 (a)/(b) 중 하나로 구현하고 3-컴파일러 비교 결과를 보고한다
- ADR-0004 에 기록

---

## ⑤ G0-24 — FSM Cancel 전이 집합과 R1 규칙

### 모순

| 위치 | 서술 |
| --- | --- |
| §4-10 주석 | `R1 AuditError : {Infer, Propose, Gate, AwaitApproval, Execute, Observe} -> Failed` — **Idle 제외** |
| §5 `ResolveUniversal` | `if (IsTerminal(from)) return false;` 후 무조건 적용 — **Idle 포함** |
| §4-10 주석 | `R2 Cancel : {Idle, Infer, Propose, Gate, AwaitApproval, Observe}` — **Idle 포함** |
| §4-10 R3 | `Execute` 는 Cancel 을 받지 않는다 |

R1 은 주석과 코드가 다르고, R2 는 주석이 Idle 을 포함한다.
`Idle` 에서 `Cancelled` 로 가면 **`turn_begin` 없는 턴에 `turn_end` 가 생긴다** — 감사 계약 위반이다.

### 확정

**전제** — `turn_begin` 커밋은 `Idle → Infer` 전이 **이전에** 수행하고,
실패하면 FSM 을 전혀 움직이지 않고 오류를 반환한다.
따라서 `Idle` 에서는 감사 실패도 취소도 발생할 수 없다.

```
[R0] Idle 에서 Cancel 은 no-op 다. 전이도 감사도 없고 Dispatch 는 Ok 를 반환한다.
     진행 중인 턴이 없으므로 취소할 대상이 없다.

[R1] AuditError : {Infer, Propose, Gate, AwaitApproval, Execute, Observe} -> Failed
     Idle 과 종료 상태는 대상이 아니다.

[R2] Cancel     : {Infer, Propose, Gate, AwaitApproval, Observe} -> Cancelled
     Idle(R0) 과 Execute(R3) 와 종료 상태는 대상이 아니다.

[R3] Execute 는 Cancel 을 이벤트로 받지 않는다.
     실행 중 취소는 CancelToken 으로 Invoker 에 전달되고 ToolResult 로 분류된다.
       effect == None  -> ToolResultStatus::Cancelled
       effect != None  -> ToolResultStatus::Indeterminate      (불변식 9)
     둘 다 Event::ExecErrorOrIndeterminate 로 Observe 에 진입한다.
     Observe 에서 CancelToken 이 여전히 set 이면 R2 로 Cancelled 로 간다.

[R4] 종료 상태(Done/Failed/Cancelled)에서 AuditError 와 Cancel 은 no-op 다.
     이미 끝난 턴을 다시 실패시키지 않는다. Dispatch 는 Ok 를 반환한다.
```

**부수 확정 — `ResetForNextTurn()` 직접 대입 제거** (체크리스트 S3-05)

```diff
-  void ResetForNextTurn() noexcept { s_ = State::Idle; }
+  // 정상 경로에서는 Dispatch(StartNextTurn) 만 사용한다.
+  // 직접 대입은 테스트 픽스처 구성 전용이며 감사를 남기지 않는다.
+  void ResetForTestOnly(State s) noexcept { s_ = s; }
```

`Done/Failed/Cancelled → Idle` 은 `Event::StartNextTurn` Dispatch 로만 일어나며,
다른 전이와 동일하게 `TransitionRecord` 를 만들고 감사된다.

### 명세 수정

§4-10 의 주석 블록과 §5 `ResolveUniversal` 을 위 R0~R4 로 교체한다.
전문은 `include/cogito/fsm.hpp` 참조.

### 영향

- `VerifyTableIntegrity()` 는 R0~R4 로 생성되는 전이까지 포함해 도달성을 검사한다
- `DumpTable()` 은 명시 19개 + R0~R4 로 생성되는 전이를 **모두** 내보낸다
  (§12-9: 대시보드 FSM 그래프가 이 출력만 쓰므로, 빠지면 화면이 실제와 달라진다)
- **테스트 추가**: `Idle` 에서 Cancel → 상태 불변 + 감사 0건, 종료 상태에서 AuditError → 상태 불변

---

## ⑥ G0-25 — 누락된 핵심 헤더 계약

### 모순

§2 저장소 레이아웃은 `invoker.hpp` · `ops_log.hpp` · `context_compactor.hpp` 를 열거하지만
§4 계약 전문에 `ops_log.hpp` 와 `context_compactor.hpp` 가 없고,
`invoker.hpp` 는 `ToolCallContext` 가 §4-12 에 흩어져 있다.

Codex 는 이 상태로 `.cpp` 를 쓸 수 없다.

### 확정

3개 헤더의 완전한 계약을 작성한다. 전문은 다음 파일이다.

| 파일 | 확정 내용 |
| --- | --- |
| `include/cogito/invoker.hpp` | `ToolCallContext`, `ToolInvoker`, Permit 소비 시점, 예외 정책, output schema 검증 시점 |
| `include/cogito/ops_log.hpp` | `LogLevel`, `OpsLogger` 인터페이스, **비밀값 유입 금지 계약**, 감사와의 분리 |
| `include/cogito/context_compactor.hpp` | `CompactionResult`, 보존 필수 항목, **버전 문자열**(골든 리플레이 키 구성요소) |

**핵심 결정 3가지**

1. **`ToolInvoker::Invoke` 는 `noexcept` 가 아니지만 예외를 밖으로 내보내지 않는다.**
   내부에서 모든 예외를 잡아 `ToolResult{status=Error}` 로 변환한다(G0-09 규칙 3).
2. **output schema 검증은 handler 반환 직후 Invoker 안에서 수행한다**(체크리스트 G0-27).
   크기 상한 → JSON 파싱 → output schema 순. 위반 시 `ToolResultStatus::Error` 이며
   대화에는 원문 대신 구조화된 `invalid_tool_result` 만 주입한다(요구사항 §3).
3. **`ContextCompactor` 는 버전 문자열을 노출한다.** §10-1 골든 리플레이 키의 구성요소이며,
   버전이 바뀌면 골든이 깨져야 한다.

---

## ⑦ G0-26 — 도메인 태그와 Digest Projection

### 모순

§6-1 은 `action`/`permit`/`audit` 세 digest 만 계산식을 보이고,
§4-3 `domain::` 은 8개 태그를 선언하지만 **`policy`·`registry`·`toolschema`·`config`·`model` 의
projection(어떤 필드를 어떤 순서로 넣는가)이 정의되지 않았다.**

정의 없이 구현하면 등록 순서·map 순회 순서·enum 숫자값이 digest 에 새어 들어간다.

### 확정 — **되돌림 불가**

**도메인 태그 9개** (기존 8 + G0-05 의 `kOperation`)

| 상수 | 값 | 대상 |
| --- | --- | --- |
| `kAction` | `cogito-action-v1` | Action 고유 식별 |
| `kOperation` | `cogito-operation-v1` | **신설.** 도구+인자 (턴 무관) |
| `kPermit` | `cogito-permit-v1` | 실행 허가 범위 |
| `kAudit` | `cogito-audit-v1` | 감사 체인 링크 |
| `kPolicy` | `cogito-policy-v1` | 정책 스냅샷 |
| `kRegistry` | `cogito-registry-v1` | 도구 레지스트리 스냅샷 |
| `kToolSchema` | `cogito-toolschema-v1` | 개별 도구 스키마 |
| `kConfig` | `cogito-config-v1` | 설정 스냅샷 |
| `kModel` | `cogito-model-v1` | 모델·가중치·템플릿 |

**태그는 재사용하지 않는다.** 서로 다른 대상이 같은 태그를 쓰면 교차 위조가 가능해진다.

**LP 인코딩 (§6-1 유지, 명문화)**

```
H(f₁ … fₙ) = SHA-256( Σᵢ [ u32le(len(fᵢ)) ‖ fᵢ ] )
  - 첫 필드는 반드시 도메인 태그
  - 문자열/바이트: UTF-8 원문 바이트
  - 정수: u64le 고정 8바이트 (십진 문자열 금지)
  - Digest: 원시 32바이트 (hex 문자열 금지)
```

**Projection 표 — 무엇을 넣고 무엇을 넣지 않는가**

| digest | 포함 필드 (이 순서) | 명시적 제외 |
| --- | --- | --- |
| `action` | tag, session_id, turn_id(u64), action_id, tool_name, CCJ(arguments) | provider_id, response_id, 수신 시각 |
| `operation` | tag, tool_name, CCJ(arguments) | session/turn/action, 시각 |
| `permit` | tag, action_digest, subject_id, mode(u64), policy_digest, registry_digest | 만료 시각, timeout |
| `audit` | tag, prev_hash, event_id, session_id, turn_id(u64), action_id, wall_time_utc, monotonic_ns(u64), process_epoch_id, kind, actor_type(u64), actor_id, CCJ(payload), schema_version(u64) | seq (DB 채번값) |
| `toolschema` | tag, name, CCJ(input_schema), CCJ(output_schema 또는 `null`) | description, handler |
| `registry` | tag, 그리고 **이름 오름차순**으로 각 도구마다: name, status, forbidden_reason, toolschema_digest, grammar_coverage, effect, risk, idempotency, approval_required, timeout_ms(u64), max_output_bytes(u64), provider_id, invoker_id | **handler 주소, 객체 주소, map 삽입 순서** |
| `policy` | tag, schema_version(u64), default_decision, 그리고 **(priority 내림차순, 동률은 rule_id 오름차순)** 으로 각 규칙의 정규화 필드 전체 | 파일 내 원래 순서, 주석 |
| `config` | tag, schema_version(u64), 그리고 CCJ(정규화된 config 객체 — `*_ref` 필드는 **참조 문자열만**) | **비밀값 원문**, 절대 경로 |
| `model` | tag, provider_id, model_id, weights_sha256, chat_template_digest, tokenizer_digest, quantization | endpoint URL, API key |

**표현 규칙 3가지**

1. **enum 은 숫자가 아니라 고정 소문자 문자열**로 projection 한다
   (`"write"`, `"high"`, `"full"`). 숫자값은 enum 재정렬 시 조용히 바뀐다.
2. **누락된 optional 필드는 빈 문자열 `""` 로 통일**한다.
   생략/`null`/`""` 를 섞으면 같은 의미가 다른 digest 를 낸다.
   LP 인코딩이 길이를 앞에 붙이므로 `""` 와 필드 없음은 구분된다 — 그래서 **항상 넣는다**.
3. **정렬 기준을 명시**한다. registry 는 name 오름차순, policy 는 (priority 내림, rule_id 오름).
   삽입 순서에 의존하면 같은 내용이 다른 digest 를 낸다.

### 영향

- `include/cogito/digest.hpp` 에 태그 9개 + projection 함수 9개 선언
- §10-2 `canonical/digest_vectors` 에 **9개 digest 각각의 고정 벡터** + 필드별 mutation 테스트
- **Codex 인계 필요**: projection 은 단일 serializer 로 구현하고 호출부에서 필드를 조립하지 않는다

---

## ⑧ G0-29 — ExecutionMode × Effect 권한 상한

### 모순

§4-7 은 "Mode 는 상한을 낮추는 방향으로만 작용한다"고 하고,
§8-4 [S-1] 은 `min(config.mode, requested_mode)` 로 계산하라고 한다.

그런데 enum 은 `Default=0, Plan=1, Edit=2, ReadOnly=3` 이다.
**숫자 순서가 권한 순서가 아니다.**

```
min(Default=0, ReadOnly=3) = Default   ← ReadOnly 를 요청했는데 Default 로 넓어진다
```

정확히 반대로 동작한다.

### 확정

**모드를 숫자로 비교하지 않는다.** 모드를 **허용 effect 상한**으로 사상한 뒤,
그 상한들의 최솟값을 취한다. `Effect` 는 `none < write < destructive` 로 선형 정렬되므로
최솟값이 잘 정의된다.

**모드 → effect 상한 (4행)**

| `ExecutionMode` | `max_effect` | 비고 |
| --- | --- | --- |
| `ReadOnly` | `none` | |
| `Plan` | `none` | ReadOnly 와 **권한상 동일**하다. 차이는 프롬프트 지시이지 권한이 아니다 |
| `Edit` | `write` | |
| `Default` | `destructive` | |

> `Plan` 과 `ReadOnly` 가 권한상 같다는 것을 명시하는 이유: 다르다고 착각하면
> "Plan 에서는 되는데 ReadOnly 에서는 안 되는" 규칙을 만들려다 정책이 왜곡된다.

**결합 규칙**

```
effective_cap = min_effect( cap(config.mode), cap(subject.requested_mode) )
```

**진리표 4×3 — Gate 5단계에서 `mode_denied` 판정**

| mode \ tool.effect | `none` | `write` | `destructive` |
| --- | --- | --- | --- |
| `ReadOnly` | 통과 | **Deny** | **Deny** |
| `Plan` | 통과 | **Deny** | **Deny** |
| `Edit` | 통과 | 통과 | **Deny** |
| `Default` | 통과 | 통과 | 통과 |

**결합표 4×4 — `effective_cap`**

| config \ requested | `Default` | `Edit` | `Plan` | `ReadOnly` |
| --- | --- | --- | --- | --- |
| `Default` | `destructive` | `write` | `none` | `none` |
| `Edit` | `write` | `write` | `none` | `none` |
| `Plan` | `none` | `none` | `none` | `none` |
| `ReadOnly` | `none` | `none` | `none` | `none` |

**요청 모드는 상한을 절대 높이지 못한다** — 표의 어느 칸도 해당 행의 config 상한을 넘지 않는다.

**역할(role)과의 관계 — 순서가 중요하다**

```
Gate 5단계:
  ① effective_cap 계산 (위 결합표)
  ② tool.effect > effective_cap  ->  Deny(mode_denied)     ← 역할과 무관하게 먼저
  ③ PolicyEngine.Decide(subject.roles, ...)  ->  Allow / Ask / Deny
```

**모드는 역할을 확장하지 못하고, 역할도 모드 상한을 넘지 못한다.**
②를 ③보다 먼저 두는 이유는, 정책이 `allow` 여도 모드 상한을 넘을 수 없어야 하기 때문이다.

### 명세 수정

§4-7 `identity.hpp` 주석과 §8-4 [S-1] 의 `min(config.mode, requested_mode)` 표현을
위 결합 규칙으로 교체한다. `ExecutionMode` 에 대한 `operator<` 나 정수 캐스팅 비교를 금지한다.

```diff
-- `requested_mode`는 **권한이 아니다.** `min(config.mode, requested_mode)`로 적용하며 올릴 수 없다.
++ `requested_mode`는 **권한이 아니다.** 모드를 effect 상한으로 사상한 뒤
++  `min_effect(cap(config.mode), cap(requested_mode))` 를 적용한다.
++  ExecutionMode 를 정수로 비교하는 코드를 금지한다(enum ordinal 은 권한 순서가 아니다).
```

### 영향

- **정적 검색 규칙 추가**: `static_cast<int>(mode)` · `mode <` · `std::min(mode` 패턴을 CI 가 실패시킨다
- **테스트**: 4 mode × 4 requested × 3 effect = 48행 table-driven (체크리스트 S4-01)

---

## ⑨ G0-31 — 승인 재진입 카운터

### 모순

§6-2 Gate 7단계는 `in.gate_reentry_count >= 1` 이면 `approval_reentry_exceeded` 로 Deny 한다.
§4-12 는 `std::map<ActionId,int> gate_reentry_` 를 갖는다.

그러나 **언제 증가하는지, 언제 초기화되는지가 어디에도 없다.**
초기 `Ask` 에서 증가시키면 첫 승인 후 재진입이 즉시 상한에 걸려 정상 흐름이 깨진다.

### 확정

**증가 시점 — `AwaitApproval → Gate` 전이(`Event::Approved`) 를 Dispatch 할 때, owner thread 가 증가시킨다.**

초기 `Ask`(Gate → AwaitApproval)에서는 **증가하지 않는다.** 그건 "재진입"이 아니라 최초 진입이다.

**정상 흐름 (승인 → 실행)**

```
1회차 Gate 진입   count=0   유효 승인 없음        -> Ask       -> AwaitApproval
승인 수신         (count 아직 0)
Event::Approved   count 0 -> 1                                  -> Gate
2회차 Gate 진입   count=1   FindUsable 이 유효 승인 발견 -> Allow  (count 미사용)
```

**비정상 흐름 (승인 대기 중 정책·Registry 변경)**

```
2회차 Gate 진입   count=1   FindUsable 이 nullptr (scope digest 불일치)
                            -> 다시 Ask 가 필요한 상황
                            -> count >= 1 이므로 Deny(approval_reentry_exceeded)
                            -> pending 을 새로 만들지 않는다
```

**상한은 1이다.** 승인 절차가 반복되면 운영자가 같은 화면을 반복해서 보게 되고,
그 자체가 승인 피로(approval fatigue)를 만든다. 두 번째 `Ask` 는 중단이 안전하다.

**초기화 시점 (2단계)**

| 시점 | 동작 |
| --- | --- |
| Action 이 `Observe` 에 도달할 때 (Allow/Deny/Reject/Expire/Exec 무관) | 해당 `action_id` 항목을 **erase** 한다 |
| `turn_end` 커밋 성공 시 | `gate_reentry_` 전체를 **clear** 한다 |

erase 를 하지 않으면 장기 세션에서 map 이 무한 증가한다(검증보고서 지적).
새 `action_id` 에는 이전 카운터가 전파되지 않는다.

**원자성**

`gate_reentry_` 는 owner thread 전용이므로 atomic 이 필요 없다([T-1]).
규범은 **"카운터 증가와 그 전이의 감사 기록은 같은 owner-thread 스텝에서 일어난다"** 이다.
증가만 하고 감사 전에 예외로 빠지는 경로가 없어야 한다.

**`reason_code` 구분 (검증보고서 지적 반영)**

`FindUsable` 이 nullptr 를 반환하는 이유가 여러 가지인데 전부 `approval_reentry_exceeded` 로
뭉개면 조사 시 원인을 알 수 없다. 다음 순서로 판정한다.

```
① 승인이 만료됨            -> approval_expired
② 승인이 이미 소비됨       -> approval_already_consumed
③ scope digest 불일치      -> approval_scope_mismatch      (정책/Registry 변경)
④ 유효 승인 없음 + count>=1 -> approval_reentry_exceeded
⑤ 유효 승인 없음 + count==0 -> Ask (정상)
```

### 명세 수정

§6-2 Gate 7단계와 §4-8 `ApprovalStore::FindUsable` 에 위 판정 순서를 명문화하고,
§4-12 `gate_reentry_` 에 증가·초기화 시점을 주석으로 고정한다.

### 영향

- **테스트 (체크리스트 S4-06)**: 3회 연속 시퀀스, 정책 변경 후 재승인, 새 action 에 카운터 미전파
- §3-4 `reason_code` 표는 이미 5개를 모두 갖고 있다 — 추가 없음

---

## 상호 정합성 확인

9건이 서로 모순되지 않는지 교차 확인했다.

| 쌍 | 확인 |
| --- | --- |
| ④ ↔ ② | CCJ 출력이 `operation_digest` 의 입력이다. ④ 정정이 ②의 벡터를 바꾼다 → ADR-0004 에 함께 기록 |
| ④ ↔ ⑦ | 모든 projection 이 CCJ 를 쓴다. ④ 확정 없이 ⑦ 벡터를 만들 수 없다 → **④를 먼저 승인** |
| ② ↔ ⑦ | `kOperation` 이 태그 목록에 편입됨 (8 → 9) |
| ③ ↔ ⑥ | `invoker.hpp` 의 예외 규칙이 ③ 규칙 3을 그대로 따른다 |
| ⑤ ↔ ⑥ | `Execute` 중 취소가 `ToolResult` 로 흡수되므로 `ToolInvoker` 가 `CancelToken` 을 받아야 한다 |
| ⑤ ↔ ⑨ | `Observe` 도달 시 `gate_reentry_` erase — R2/R3 로 Observe 에 오는 경로도 포함 |
| ⑧ ↔ ⑨ | 둘 다 Gate 5·7단계다. ⑧(mode)이 ⑨(승인)보다 먼저 판정된다 |
| ① ↔ ⑧ | `cogito_subject_t.requested_mode` 의 의미가 ⑧로 확정됨 |

**모순 없음.** 승인 순서 권고: **④ → ② → ⑦ → 나머지**.

---

## 승인 요청 사항

| 결정 | 승인자 | 이유 |
| --- | --- | --- |
| ④ CCJ 지수 규칙 + 골든표 2행 정정 | 아키텍트 | 되돌림 불가. 모든 digest 의 기반 |
| ② operation_digest 신설 | 아키텍트 + 안전 책임자 | 되돌림 불가. `indeterminate` 안전 통제의 실효성 |
| ⑦ 도메인 태그 9개 + projection | 아키텍트 | 되돌림 불가. 감사 체인 검증 가능성 |
| ①③⑤⑥⑧⑨ | 아키텍트 | 되돌림 가능 |

**이 보고서로 닫히지 않는 것** — G0-02·03·04·06·07·08·10~22·27·28·30·32·33 (24건).
그중 `G0-13`(llama.cpp 공급망) · `G0-14`(OPC UA 실설비값) · `G0-17`(인증원) ·
`G0-22`(CI·서명 주체) · `G0-16`(릴리스 범위)는 **AI 가 정할 수 없다.**
