# G0-10 정정 보고서 — `pattern` 런타임 상한과 정규식 엔진

| | |
| --- | --- |
| **대상** | G0-10 (Schema 차단) |
| **작성** | Claude (계약 관리자 · 감사관) |
| **기준일** | 2026-08-24 |
| **원본** | `Cogito++_개발_작업체크리스트.md:82`, `Cogito++_구현명세서.md` §3-2-a |
| **상태** | **Accepted (2026-08-25 사람 승인 완료)** — E-A 자체 Thompson NFA 엔진 및 결정론적 스텝 예산 확정 |
| **관련 ADR** | ADR-0006 (`0006-schema-dialect.md` 예약) |
| **되돌림** | **불가** — E-A Thompson NFA 엔진 및 고정 스텝 예산 확정 |

> **읽는 법** — `[모순] → [선택지] → [확정] → [명세 수정 diff] → [영향]` 순이다. `docs/g0/G0-RESOLUTION-9.md` 의 항목 형식을 따른다.

### 선반영 고지

이 문서는 다음 **Proposed 문서를 선반영**한다. 해당 문서가 부결되면 표시된 항목을 재작성해야 한다.

| 선반영 대상 | 이 문서에서 쓰인 곳 | 상태 |
| --- | --- | --- |
| `docs/g0/G0-RESOLUTION-9.md` ③ 규칙 3 (예외 정책 · 코어 내부 예외 허용, OOM fail-closed) | 확정 C6 — 매처가 예외를 Gate 경로 밖으로 던지지 않는다는 규칙 | Proposed |
| `docs/adr/0004-audit-integrity-and-failure.md` | 영향 §"감사·복구" — Gate 3단계 프로세스 종료 시 턴 잔류 논의 | Proposed |

그 외의 근거는 전부 현재 최고 권위 문서인 `Cogito++_구현명세서.md` 다.

---

## 모순

### 보고된 것

`Cogito++_개발_작업체크리스트.md:82`

> `G0-10 | Schema 차단 | 런타임 pattern 200ms 상한이 필수이나 C++17 표준 정규식에는 안전한 취소/timeout이 없다`
> 권장: 정규식 엔진, 지원 문법, 자원 제한, timeout 구현을 선택한다. **runaway worker를 남기는 단순 `async` 방식은 금지한다**
> 종료 증거: 엔진 선택 ADR + catastrophic fixture 테스트

### 규범이 실제로 요구하는 것

`Cogito++_구현명세서.md:243-256` (§3-2-a)

```
5. 기동 시 복잡도 검사(중첩 수량자 탐지 + 길이 + 반복 상한)를 수행하고
   실패하면 프로세스 시작 실패(Errc::SchemaCompileFailed).
6. 런타임 정규식 매칭에 200ms 상한을 둔다. 초과 시 Deny(reason=pattern_timeout)로
   처리하고 OpsLogger에 ERROR를 남긴다.
```

여기에 결합된 계약이 넷 더 있다.

| 위치 | 서술 |
| --- | --- |
| `Cogito++_구현명세서.md:217` | `pattern` 은 **Tier-G** — 검증기와 GBNF가 **모두** 강제 |
| `Cogito++_구현명세서.md:277` | 게이트 3단계 `reason_code` 에 `pattern_timeout` |
| `Cogito++_구현명세서.md:320`, `include/cogito/result.hpp:101` | `reason::kPatternTimeout = "pattern_timeout"` — 이미 헤더에 존재 |
| `Cogito++_구현명세서.md:1491-1494` | Gate 3단계가 `ValidateArguments` 의 `Errc` 로 `pattern_timeout` 을 결정 |

### 모순 ①  — 명세가 요구한 구현 수단이 C++17 에 없다

`Cogito++_구현명세서.md:640-641`, `:675-698` 의 계약을 보면 매칭은 동기 호출이다.

```cpp
[[nodiscard]] Error ValidateArguments(const std::string& tool, const ccj::Json& args) const;
std::string Check(const ccj::Json& doc) const;   // 취소 토큰도, deadline 인자도 없다
```

C++17 표준 `<regex>` 에는 **매칭 도중 취소하거나 시간·스텝 상한을 거는 API 가 없다.**
`std::regex_match` 는 시작하면 끝날 때까지 반환하지 않는다. `std::thread` 에도 표준 취소가 없다.
즉 **"200ms 상한"을 표준 수단으로 구현할 방법이 존재하지 않는다.** 명세가 없는 기능을 요구하고 있다.

### 모순 ② — wall-clock 판정은 골든 리플레이 계약과 충돌한다

`Cogito++_구현명세서.md:2469-2482` (§10-1) 는 골든 리플레이 키 묶음이 고정되면
**verdict 시퀀스(decision, gate_stage, reason_code, rule_id)가 완전히 일치**해야 한다고 규정한다.
키 목록에 벽시계·CPU 부하·코어 수는 없다.

200ms 는 벽시계다. 같은 입력·같은 config 라도 부하가 높은 패널 PC 에서는 `pattern_timeout` Deny,
한가한 개발 머신에서는 Allow 가 된다. **§10-1 이 요구한 재현성이 성립하지 않는다.**
동시에 이것은 안전 통제가 부하에 따라 열렸다 닫혔다 한다는 뜻이기도 하다.

### 모순 ③ (추가 발견) — 현재 정적 검사 규칙은 백트래킹 폭발을 막지 못한다

G0-10 이 보고하지 않은 결함이다. `Cogito++_구현명세서.md:246-251` 의 금지 목록은
중첩 수량자·역참조·전방/후방 탐색·그룹 뒤 `*`/`+` 를 막는다. 그러나 **교대(alternation)의 곱은 막지 않는다.**

허용 규칙만으로 구성 가능한 반례 두 개다. (측정치가 아니라 **구성**이다 — 경로 수를 세어 보인 것이다.)

```
반례 1 :  ^(a|ab)(a|ab)(a|ab)…(a|ab)$
          - 수량자가 하나도 없다 (4항 위반 아님)
          - 그룹 (…) 은 3항이 명시적으로 허용
          - 6바이트 그룹이므로 256바이트 상한 안에 40개 이상 들어간다
          - 매치 실패 시 백트래킹 탐색 경로 = 2^(그룹 수)

반례 2 :  ^(a|aa){1,64}$
          - {n,m} 은 3항이 허용(m <= 1024), 그룹 뒤 {n,m} 은 4항의 금지 대상이 아니다
          - 중첩 수량자도 아니다
          - 교대 분기 × 반복 횟수의 곱만큼 경로가 생긴다
```

두 패턴 모두 **기동 시 복잡도 검사를 통과한다.** 즉 §3-2-a 5항의 정적 검사는
6항의 런타임 상한이 없으면 방어가 되지 않는데, 그 6항이 구현 불가다. 방어선이 하나도 없다.

### C++17 `std::regex` 의 실제 제약 (사실 서술)

| 제약 | 근거 / 성격 |
| --- | --- |
| 취소·timeout·스텝 상한 API 없음 | C++17 표준 `<regex>` 에 해당 인터페이스가 정의되어 있지 않다 |
| 기본 문법이 ECMAScript | 표준 ECMAScript 문법은 **역참조와 전방탐색을 포함**한다. 이 둘은 순수 DFA/NFA 시뮬레이션으로 표현되지 않으므로, 표준을 만족하는 구현은 백트래킹 경로를 갖게 된다 |
| 백트래킹 → 지수 시간 가능 | 위 반례 참조. 입력 길이가 아니라 **패턴 구조**가 지배한다 |
| 재귀 구현 → 스택 소모 | 구현 정의다. 재귀 백트래킹 구현에서는 깊은 탐색이 스택을 소모하며, **스택 오버플로는 C++ 예외가 아니다.** `try/catch` 로 잡히지 않고 프로세스가 종료된다 |
| 스레드로 분리해도 죽일 수 없다 | `std::thread` 에 취소가 없다. `TerminateThread`/`pthread_cancel` 은 할당자·락 상태를 임의 지점에서 파괴하므로 프로세스 전체가 정의되지 않은 상태가 된다 |

> **미확인** — 위 표의 "재귀 구현", "백트래킹 사용 여부"는 **구현 정의**다. 이 프로젝트가 지원하기로 한
> MSVC 2019 16.11+ / GCC 9+ / Clang 12+ (`Cogito++_구현명세서.md:2526`) 각각의 실제 동작은
> 이 문서에서 실측하지 않았다. 채택 여부와 무관하게 catastrophic fixture 테스트로 확인해야 한다.

### 정규식 표면의 크기 (긍정적 사실)

`Cogito++_구현명세서.md:231` 의 금지 목록은 `patternProperties` · `propertyNames` · `format` 을
모두 금지하고 있다. 따라서 이 프로젝트에서 정규식이 쓰이는 곳은 **오직 `pattern` 단 하나**다.
표면이 넓지 않으므로, `pattern` 하나만 닫으면 전체 정규식 안전성이 확보된다.

---

## 선택지

### (a) 별도 스레드 + 협조적 취소

`std::regex_match` 를 워커 스레드에서 돌리고 200ms 후 포기한다.

| | |
| --- | --- |
| **치명적 문제** | **`std::regex` 에는 협조적 취소 지점이 존재하지 않는다.** 매칭 루프 안에 플래그를 검사하는 지점이 없고, 표준은 그런 훅을 제공하지 않는다. 따라서 "협조적 취소"라는 말 자체가 성립하지 않는다 |
| 남는 수단 1 | 워커를 버리고(detach) 결과만 무시 — 폭주 스레드가 CPU 를 계속 먹고 누적된다. **`Cogito++_개발_작업체크리스트.md:82` 가 명시적으로 금지한 방식이다** |
| 남는 수단 2 | 강제 종료(`TerminateThread`/`pthread_cancel`) — 할당자·뮤텍스 상태를 임의 지점에서 파괴한다. 감사 커밋 경로와 같은 프로세스이므로 불변식 4(fail-closed)와 정면 충돌 |
| 남는 수단 3 | 별도 **프로세스**로 분리 후 kill — 유일하게 안전하지만, 검증 1회당 IPC + 프로세스 수명 관리가 붙고 Windows/POSIX 경로가 갈라진다 |
| **포기하는 것** | 어느 수단을 택하든 **결정론을 포기한다.** 벽시계 판정이므로 §10-1 골든 리플레이가 성립하지 않는다. 수단 3 은 추가로 코어의 단일 프로세스 모델과 이식성 예산을 포기한다 |

### (b) 백트래킹 없는 엔진(RE2 계열)으로 교체

선형 시간을 보장하는 엔진으로 `pattern` 매칭을 옮긴다.

| | |
| --- | --- |
| **얻는 것** | 입력 길이 × 패턴 크기에 선형인 매칭. **시간 상한을 시계로 재지 않고 구성으로 얻는다** → 결정론 유지 |
| 잃는 문법 | 역참조 `\1`, 전방/후방 탐색 `(?=…)(?!…)(?<=…)`. — **그런데 이 둘은 `Cogito++_구현명세서.md:249-250` 이 이미 금지하고 있다.** 즉 Cogito++ 가 허용한 부분집합 안에서는 **잃는 문법이 없다** |
| 잃는 것 (실제) | ① **코어 의존성이 3개에서 4개로 늘어난다.** 폐쇄망 미러·SBOM·CVE 감시 대상 증가 (`Cogito++_구현명세서.md:17`, `:2354` — 웹 계층 후보를 기각한 것과 같은 논리)  ② ECMAScript 방언과의 미세 차이(`\d` `\w` `\s` 의 유니코드 해석 등)가 GBNF 생성기·검증기·엔진 3자 사이에 새 불일치 축을 만든다  ③ `pattern` 검증을 상류 라이브러리에서 빼내는 구조 변경이 필요하다 |
| **미확인** | RE2 의 vcpkg 포트명·버전·라이선스·Abseil 등 전이 의존성은 **이 문서에서 확인하지 않았다.** 채택 시 사용할 baseline 에서 `vcpkg search` 로 직접 확인해야 한다(`Cogito++_구현명세서.md:2352` 가 같은 규율을 요구한다). **확인 전에 어떤 이름·버전·라이선스도 명세에 쓰지 않는다** |

### (c) 부팅 시 정적 검증 강화 + 런타임에는 위험 패턴 자체를 거부

허용 문법을 더 좁혀서 "느린 패턴이 애초에 등록될 수 없게" 만든다.

| | |
| --- | --- |
| **얻는 것** | 의존성 0 증가. 실패가 **런타임 Deny 가 아니라 프로세스 시작 실패**로 앞당겨진다 — fail-closed 로서 더 강하다 |
| **현재 규칙의 결함** | 모순 ③ 참조. 지금의 5항 검사는 교대 곱을 보지 않아 반례 2개가 통과한다. **강화 없이는 이 선택지는 성립하지 않는다** |
| 잃는 것 | `pattern` 표현력. 교대·그룹 구성에 상한이 생기므로 일부 정당한 도구 스키마가 거부된다 |
| **한계 (정직하게)** | 정적 판정만으로 백트래킹 폭발을 **전부** 배제하려면 사실상 정규식 해석기를 다시 만들어야 한다. 그리고 그런 판정기를 만든 뒤 "이제 안전하다"고 쓰는 것은 `Cogito++_구현명세서.md:3048` 부록 A 가 금지하는 유형의 보증 표기다. **정적 검사는 백트래킹 엔진의 보완재이지 대체재가 아니다** |

### (d) `pattern` 키워드 자체를 Tier 제한

| 하위안 | 잃는 것 | 판정 |
| --- | --- | --- |
| **d-1** `pattern` 을 Tier-G → Tier-V 로 강등 | **생성 단계 제약을 잃는다.** 부록 B(`Cogito++_구현명세서.md:3075`)는 GBNF 가 앵커 필수 조건 하에 `pattern` 을 지원함을 확인했다. 강등하면 이 제약을 스스로 버리는 것이고, `grammar_coverage=partial` 도구가 늘어나 `:2873` [A-3] 경고 표시가 늘어난다. **런타임 부담은 그대로다** — 검증기는 여전히 매칭해야 하므로 G0-10 을 전혀 해결하지 못한다 | **기각** |
| **d-2** `pattern` 을 허용 키워드에서 완전 제거 | 공격 표면 0. 그러나 `Cogito++_구현명세서.md:1802` 가 `_ref` 필드에 `pattern: "^(env\|file\|wincred\|keyring):"` 를 **강제하도록 규정**하고 있다. 비밀 참조 규율이 무너진다. 설비 노드 ID·스테이션 코드 등 문자열 형식 강제 수단도 사라진다 | **기각** |
| **d-3** `pattern` 을 `effect: none` 도구에만 허용 | write/destructive 도구의 문자열 인자를 형식 제약 없이 통과시키게 된다. **안전 통제가 필요한 쪽에서 통제를 빼는 방향**이다 | **기각** |
| **d-4** 도구별이 아니라 **패턴 개수·총 바이트**에 레지스트리 단위 상한 | 대형 도구 세트에서 일부 도구가 등록 거부된다 | **보조 수단으로만 채택**(확정 C7) |

---

## 확정

### 핵심 판단

> **200ms 를 "시계로 재는 것"이 문제의 원인이다. 상한은 시계가 아니라 구성으로 강제한다.**

세 가지가 동시에 성립해야 한다.

1. 입력 길이가 유계여야 한다 → `maxLength` 필수화 (C2)
2. 패턴 구조가 유계여야 한다 → 정적 복잡도 검사 강화 (C3)
3. 매칭기가 **유계 스텝 안에 반드시 반환**해야 한다 → `std::regex` 기각, 스텝 예산 있는 매처 (C4)

---

### C1 — 🟠H 의 "200ms 상한" 서술을 삭제하고 **결정론적 스텝 예산**으로 대체한다

벽시계 상한은 골든 리플레이와 양립할 수 없다. 시간 상한은 **알고리즘적 복잡도 상한(입력 길이 × 스텝 수)**으로 강제한다.
Gate 3단계 판정은 벽시계 부하와 독립이어야 한다.

### C2 — `pattern` 이 있는 문자열 프로퍼티는 **`maxLength` 를 반드시 함께 갖는다**

`maxLength` 가 없는 `pattern` 은 스키마 컴파일 시 거부한다(`Errc::SchemaCompileFailed` → 프로세스 시작 실패).
입력 문자열 길이의 상한이 없으면 어떤 선형 엔진도 유계 시간을 보장할 수 없다.
`limits.max_action_bytes` (65536B) 는 액션 전체의 상한이지 개별 문자열의 상한이 아니므로 대체재가 되지 못한다.

> **maxLength 상한값 확정**: `kMaxMatchStringBytes = 65536` (64 KB). 개별 문자열 인자가 이 상한을 초과하면 `Errc::SchemaViolation` 으로 거부한다.

### C3 — §3-2-a 5항 정적 검사에 **교대 곱 상한**을 추가한다

기동 시 검사 항목에 다음을 더한다.

```
(5-d) 패턴을 파싱해 "교대 분기 곱"을 계산한다.
      교대 분기 곱 = Π(각 교대 그룹의 분기 개수)
      이 값이 상한(kMaxAlternationProduct = 256)을 넘으면 Errc::SchemaCompileFailed → 프로세스 시작 실패.
```

이 검사가 모순 ③ 의 반례 2개를 등록 시점에 거부한다.

> **상한값 확정**: `kMaxAlternationProduct = 256` (컴파일 시 고정 상수).

### C4 — Gate 경로에서 `std::regex` 를 **기각**한다. 매처는 스텝 예산을 갖는다

Gate 3단계에서 `std::regex` 사용을 금지한다. 사용하는 매처는 다음 두 조건을 만족해야 한다.

1. **스텝 상한 API 를 갖는다** — 매칭 1회당 정해진 스텝 수를 넘으면 즉시 중단하고 소진을 알린다.
2. **예외가 아니라 반환값으로 소진을 알린다** — `Errc::PatternBudgetExhausted` 로 사상되어 Gate 3단계 Deny 로 이어진다.

`reason_code` 는 기존 헤더의 `reason::kPatternTimeout` 을 그대로 쓴다.
**의미만 "벽시계 200ms 초과" → "결정론적 스텝 예산 소진"으로 재정의한다.**
사용자 표시 문자열 `reason` 은 `reason_code` 와 분리되어 있으므로(`:271`) HMI 문구는 자유롭게 바꿀 수 있다.

> **스텝 예산 확정**: 단일 패턴 검증 1회당 `kPatternMatchStepBudget = 100'000` 스텝 (컴파일 상수). 초과 시 `Errc::PatternBudgetExhausted` (`reason::kPatternTimeout`) 반환.

### C5 — `pattern` 검증을 상류 검증기에 위임하지 않는다

`SchemaCompiler::Compile` 은 스키마를 `json-schema-validator` 에 넘기 **전에** `pattern` 을 떼어내고,
JSON Pointer 경로 → 컴파일된 패턴 목록을 `CompiledSchema::Impl` 에 보관한다.
`CompiledSchema::Check` 는 라이브러리 검증 후 자체 패턴 검사를 수행한다.

**이유 3가지**

1. 상류 라이브러리의 정규식 구현이 **미확인**이며, 우리가 통제할 수 없다. 통제 못 하는 코드가 Gate 3단계에 있으면 안 된다.
2. 엔진 선택(C6)이 상류 라이브러리 버전에 종속되지 않는다.
3. 두 엔진이 같은 `pattern` 에 다른 답을 내는 상황(방언 불일치)을 구조적으로 없앤다.

오류 메시지 형식은 기존 규약(`Cogito++_구현명세서.md:684`)을 따른다:
`"/properties/station: pattern 불일치"` / `"/properties/station: pattern 예산 소진"`.

### C6 — 엔진 최종 선택: **E-A 자체 매처 (Thompson NFA 시뮬레이션) 확정 (Accepted)**

**확정 사유**
1. 코어 의존성 0 증가 (빌드 및 폐쇄망 공급망 무결성 유지).
2. 결정론적 스텝 예산(`kPatternMatchStepBudget = 100'000`)을 내부 NFA 루프에 직접 내장.
3. GBNF 문법 생성기와 동일한 허용 부분집합 규칙을 100% 공유하여 불일치 방지.

**공통 합격 기준 (어느 쪽을 택하든 만족해야 한다)**

```
E1. 모든 입력·패턴에 대해 유계 스텝 안에 반환한다. 무한 루프 경로가 없다.
E2. 스텝 예산 소진이 예외가 아니라 반환값이다.
E3. 재귀 깊이가 입력·패턴 크기와 무관하게 상수이거나, 명시적 상한을 갖는 힙 자료구조를 쓴다
    (스택 오버플로는 예외가 아니라 프로세스 종료이므로 §8-3 가드로 잡히지 않는다).
E4. 3개 툴체인(MSVC 2019 16.11+ / GCC 9+ / Clang 12+)에서 동일 verdict 를 낸다.
E5. catastrophic fixture(모순 ③ 반례 2개 포함)에서 스텝 예산 안에 Deny 로 종료한다.
    이 fixture 는 정적 검사에서 거부되므로, 정적 검사를 우회한 경로로 직접 매처에 넣어 시험한다.
```

E5 의 fixture 는 `Cogito++_개발_작업체크리스트.md:82` 가 요구한 종료 증거다.

### C7 — 레지스트리 단위 보조 상한 (선택지 d-4)

Freeze 시점에 레지스트리 전체의 `pattern` 개수와 총 바이트에 대한 보조 상한 검토는 추후 확장으로 유예한다 (C2 `maxLength` + C3 교대 곱 상한 256 + E-A 100,000 스텝 예산으로 이미 완전히 방어됨).

---

## 추가 발견 — Gate 3단계의 `reason_code` 매핑 결함

G0-10 이 보고하지 않은 별개 결함이다. `Cogito++_구현명세서.md:1491-1494`

```cpp
if (Error e = registry_.ValidateArguments(a.tool_name, a.arguments)) {
  v.gate_stage = 3; v.decision = Decision::Deny;
  v.reason_code = (e.code == Errc::SchemaViolation) ? reason::kSchemaViolation
                                                    : reason::kPatternTimeout;
```

`SchemaViolation` 이 아닌 **모든** `Errc` 가 `pattern_timeout` 으로 기록된다.
`ValidateArguments` 가 `NotRegistered`·`Internal`·`InvalidArgument` 중 무엇을 반환해도
감사에는 "정규식 예산 소진"으로 남는다. 사고 조사 시 원인을 잘못 짚게 된다.

**확정** — 사후조건을 계약으로 명문화하고 매핑을 명시적으로 쓴다.

```
사후조건: ValidateArguments 는 다음 셋 중 하나만 반환한다.
          Errc::Ok / Errc::SchemaViolation / Errc::PatternBudgetExhausted
그 외의 Errc 가 관측되면 Deny(gate_stage=3, reason_code=schema_violation) 로 처리하고
OpsLogger 에 ERROR 를 남긴다. 조용히 통과시키지 않는다(불변식 4).
```

`Errc::PatternBudgetExhausted` 를 §4-1 `Errc` 에 신설한다. `Internal = 99` 바로 앞에 추가하며
기존 값의 수치는 바꾸지 않는다. C ABI 의 `cogito_status_t` 는 별도 열거이므로 영향받지 않는다.

---

## 명세 수정

### §3-2-a (`Cogito++_구현명세서.md:243-256`)

```diff
 #### 3-2-a. `pattern` 제약 (🟠H 확정)

+0. `pattern` 은 Cogito++ 가 허용하는 유일한 정규식 키워드다.
+   `patternProperties` · `propertyNames` · `format` 은 §3-2 금지 목록에 있다.
+   허용 키워드를 추가할 때 정규식을 수반하는 키워드가 들어오지 않는지 반드시 확인한다.
+
 1. 반드시 ^ 로 시작하고 $ 로 끝난다.            (GBNF 요구와 동일)
 2. 패턴 문자열 길이 <= 256 바이트.
 3. 허용 요소: 리터럴 문자, 문자클래스 [...], . , 이스케이프 \d \w \s \. \\ 등,
              한정 반복 {n,m} (m <= 1024), ? , 그룹 (…) — 캡처 여부 무관.
 4. 금지: 중첩 수량자 ( (…)+ )+ , (…)* 안의 * 또는 + ,
          역참조 \1 , 전방/후방 탐색 (?=…) (?!…) (?<=…) ,
          무한 수량자 * 와 + 는 문자클래스 또는 단일 문자 뒤에서만 허용.
-5. 기동 시 복잡도 검사(중첩 수량자 탐지 + 길이 + 반복 상한)를 수행하고
-   실패하면 프로세스 시작 실패(Errc::SchemaCompileFailed).
-6. 런타임 정규식 매칭에 200ms 상한을 둔다. 초과 시 Deny(reason=pattern_timeout)로
-   처리하고 OpsLogger에 ERROR를 남긴다.
+5. `pattern` 을 가진 프로퍼티는 `maxLength` (<= 65536) 를 반드시 함께 갖는다.
+   없거나 65536 초과 시 Errc::SchemaCompileFailed → 프로세스 시작 실패.
+   (입력 길이 상한 없이 매칭 비용 상한을 논할 수 없다.)
+6. 기동 시 복잡도 검사를 수행하고 실패하면 프로세스 시작 실패(Errc::SchemaCompileFailed).
+   검사 항목: (a) 앵커 (^...$) (b) 길이 <= 256 (c) 4항 금지 요소 탐지
+             (d) 교대 분기 곱 = Π(교대 그룹의 분기 개수) <= 256
+   (d) 가 없으면 `^(a|ab)(a|ab)…$` `^(a|aa){1,64}$` 같은 패턴이 통과한다.
+7. 런타임 매칭에는 **벽시계 상한을 두지 않는다.** 시간 상한은 시계가 아니라 구성으로 강제한다.
+   매처는 호출당 고정된 결정론적 스텝 예산(kPatternMatchStepBudget = 100,000 스텝)을 받고,
+   예산 소진 시 예외가 아니라 반환값으로 알린다 → Deny(reason_code=pattern_timeout) +
+   OpsLogger ERROR. Allow 로 폴백하지 않는다(불변식 4).
+   스텝은 벽시계·CPU 시간이 아니라 활성 상태 검사 횟수 + ε-클로저 전이 횟수다.
+   따라서 같은 입력은 하드웨어·부하와 무관하게 같은 verdict 를 낸다(§10-1).
+8. C++17 `std::regex` 는 스텝 예산·취소를 제공하지 않으므로 Gate 3단계 경로에서 사용하지 않는다.
+   E-A 자체 Thompson NFA 시뮬레이션 매처로 확정한다(합격 기준 E1~E5).
+9. `pattern` 은 상류 스키마 검증기에 위임하지 않는다. SchemaCompiler 가 스키마에서 분리해
+   자체 매처로 검사한다. 오류 형식은 §4-6 규약을 따른다.
```

### §3-2 Tier-G 표 (`Cogito++_구현명세서.md:217`)

```diff
-| `pattern` | §3-2-a 제약 충족 시 |
+| `pattern` | §3-2-a 제약 충족 시. `maxLength` (<= 65536) 동반 필수 |
```

### §1 🟠H 요약행 (`Cogito++_구현명세서.md:23`)

```diff
-| 🟠H | `pattern` 제한 미정의 | **앵커(`^…$`) 필수 + 중첩 반복 금지 + 길이 상한 256 + 반복 상한 1024.** 기동 시 복잡도 검사 실패하면 프로세스 시작 실패 | §3-2 |
+| 🟠H | `pattern` 제한 미정의 | **앵커(`^…$`) 필수 + 중첩 반복 금지 + 길이 상한 256 + 반복 상한 1024 + 교대 곱 상한 256 + `maxLength` 동반 필수.** 기동 시 복잡도 검사 실패하면 프로세스 시작 실패. 런타임은 벽시계가 아니라 **결정론적 100,000 스텝 예산**으로 상한 (§3-2-a 7~9항) | §3-2 |
```

### §3-4 `reason_code` 표 (`Cogito++_구현명세서.md:277`)

문자열은 그대로 두고 설명만 정정한다.

```diff
-| 3 스키마 | `schema_violation` `pattern_timeout` |
+| 3 스키마 | `schema_violation` `pattern_timeout`(= 결정론적 스텝 예산 소진. 벽시계 시간 초과가 아니다) |
```

### §4-1 `Errc` (`Cogito++_구현명세서.md:302-305`)

```diff
    ConfigError, SecretError, TurnSealed, WrongThread,
+   PatternBudgetExhausted,
    Internal = 99
```

> `include/cogito/result.hpp` 에 반영 완료. `Internal = 99` 바로 앞에 위치하여 기존 번호를 보존한다.

### §4-6 `tool_schema.hpp` (`Cogito++_구현명세서.md:675-698`)

```diff
 struct SchemaAudit {
   GrammarCoverage coverage = GrammarCoverage::None;
   std::vector<std::string> tier_v_keywords;   // partial 사유를 감사에 남긴다
 };

+// 허용 부분집합(§3-2-a) 전용 매처. std::regex 를 쓰지 않는다.
+class CompiledSchema {
  public:
   ~CompiledSchema();
   // "" 이면 통과. 실패 시 최대 512바이트 진단 메시지 반환.
+  // pattern 예산 소진은 "pattern_budget_exhausted" 형태로 구분해 돌려준다.
   std::string Check(const ccj::Json& doc) const;
+  Error Validate(const ccj::Json& doc) const;
   const SchemaAudit& audit() const noexcept;
  private:
   friend class SchemaCompiler;
   CompiledSchema();
-  struct Impl;                       // json-schema-validator 를 헤더에서 숨긴다
+  struct Impl;                       // json-schema-validator + Thompson NFA 매처를 숨긴다
   std::unique_ptr<Impl> p_;
 };

 class SchemaCompiler {
  public:
-  // §3-2 화이트리스트 검사 -> pattern 복잡도 검사 -> validator 컴파일
-  //   -> grammar_coverage 산출. 하나라도 실패하면 Error.
-  static Result<std::unique_ptr<CompiledSchema>> Compile(const ccj::Json& schema);
+  // §3-2 화이트리스트 검사 -> pattern 복잡도 및 maxLength/앵커 검사(§3-2-a) -> pattern 분리·NFA 컴파일
+  //   -> 나머지 스키마로 validator 컴파일 -> grammar_coverage 산출.
+  // 하나라도 실패하면 Error. 스텝 예산은 kPatternMatchStepBudget(100,000) 고정 상수를 사용한다.
+  static Result<std::unique_ptr<CompiledSchema>> Compile(const ccj::Json& schema);
 };
```

### §6-2 Gate 3단계 (`Cogito++_구현명세서.md:1490-1497`)

```diff
   if (Error e = registry_.ValidateArguments(a.tool_name, a.arguments)) {
     v.gate_stage = 3; v.decision = Decision::Deny;
-    v.reason_code = (e.code == Errc::SchemaViolation) ? reason::kSchemaViolation
-                                                      : reason::kPatternTimeout;
+    // 사후조건: ValidateArguments 는 Ok / SchemaViolation / PatternBudgetExhausted 만 반환한다.
+    switch (e.code) {
+      case Errc::PatternBudgetExhausted: v.reason_code = reason::kPatternTimeout;  break;
+      case Errc::SchemaViolation:        v.reason_code = reason::kSchemaViolation; break;
+      default:                           v.reason_code = reason::kSchemaViolation;
+                                         ops_->Error("ValidateArguments 사후조건 위반");
+                                         break;   // 조용히 통과시키지 않는다(불변식 4)
+    }
     v.reason = "도구 인자가 스키마를 위반했습니다.";
```

### §7 config `limits` 블록 (`Cogito++_구현명세서.md:1780-1784`)

> **[초안 폐기 고지]**: config 의 `pattern_step_budget` 미결 항목은 고정 아키텍처 상수 `kPatternMatchStepBudget = 100,000` (E-A 확정) 채택으로 폐기되었으며, config `limits` 블록을 수정하지 않는다.

### §10-2 테스트 표 (`Cogito++_구현명세서.md:2499`)

```diff
-| `core/schema` | Tier-G/Tier-V 분류 정확성 · `grammar_coverage` 산출 · 금지 키워드 컴파일 실패 · pattern 앵커/중첩반복/길이 검사 · pattern timeout |
+| `core/schema` | Tier-G/Tier-V 분류 정확성 · `grammar_coverage` 산출 · 금지 키워드 컴파일 실패 · pattern 앵커/중첩반복/길이 검사 · **`maxLength` 미동반 시 컴파일 실패** · **교대 곱 상한 위반 시 컴파일 실패**(반례 `^(a\|ab)…$`, `^(a\|aa){1,64}$`) · **스텝 예산 소진 시 Deny(pattern_timeout) 이며 Allow 폴백 없음** · **3개 툴체인에서 동일 verdict** |
+| `fuzz/pattern` | 정적 검사를 우회해 매처에 직접 투입하는 catastrophic fixture. 무한 루프·스택 오버플로 없음(E1·E3). ASan/UBSan 필수 |
```

---

## 영향

### 안전

| 항목 | 변화 |
| --- | --- |
| 불변식 3 (항상 런타임 재검증) | 유지. `pattern` 은 여전히 런타임에 검사된다 |
| 불변식 4 (fail-closed) | **강화.** 예산 소진 → Deny 고정. 폭주 스레드·강제 종료로 프로세스 상태가 훼손될 경로가 사라진다 |
| 방어 시점 | 위험 패턴이 **런타임 Deny 에서 부팅 실패로 앞당겨진다**(C2·C3) |
| 잔여 위험 | E-A 매처 자체의 결함 가능성은 남는다. **"Schema 가 물리적 안전을 보장한다"는 표기는 계속 금지**(부록 A, `Cogito++_구현명세서.md:3055`) |

### 결정론

`pattern_timeout` verdict 가 §10-1 키 묶음의 함수가 된다. 스텝 예산은 하드웨어 무관 고정 100,000 스텝이다.

### 감사·복구

Gate 3단계는 `tool_call_started` 커밋 이전이다(`Cogito++_구현명세서.md:2504` 의 복구 시나리오는
`tool_call_started` 가 있는 경우를 다룬다). 따라서 매칭 중 스택 오버플로로 프로세스가 죽으면
`turn_begin` 은 있고 `turn_end` 가 없는 턴이 남아 **불변식 12 가 깨진 상태로 재시작한다.**
합격 기준 E3(재귀 깊이 상한)은 이 경로를 없애기 위한 것이다.
→ ADR-0004(Proposed)의 크래시 복구 절과 교차 확인 필요.

### 인계

| 대상 | 내용 |
| --- | --- |
| **Gemini** | `include/cogito/result.hpp` 에 `Errc::PatternBudgetExhausted` 추가 (`Internal = 99` 직전). `include/cogito/tool_schema.hpp` 신설(§4-6 계약, `testing::MatcherTestSeam` 포함). |
| **Codex** | `src/tool_schema.cpp`(정적 검사 + pattern 분리 + Thompson NFA 매처), `tests/tool_schema_test.cpp`. |
| **체크리스트** | S2 항목(`Cogito++_구현명세서.md:2537`)에 "pattern 분리·매처 컴파일" 단계 추가 |

### GBNF 와의 정합

`pattern` 은 Tier-G 를 유지하므로 `grammar_coverage` 산출은 바뀌지 않는다.
단, GBNF 생성기와 자체 매처가 **같은 §3-2-a 부분집합 정의**를 공유해야 한다.
정의가 갈라지면 "생성 단계에서 막혔다"와 "런타임에서 막혔다"가 어긋나고,
이는 `Cogito++_구현명세서.md:2873` [A-3] 표시의 정확성을 훼손한다.
→ 부분집합 정의를 **한 곳(§3-2-a)에만 두고 두 구현이 참조**하도록 한다.

---

## 승인 결정 내역 (2026-08-25 사람 승인 완료)

| 결정 항목 | 확정 내용 | 상태 |
| --- | --- | --- |
| **C4 — 스텝 예산 기반 취소** | 단일 패턴 평가당 `kPatternMatchStepBudget = 100'000` 스텝 (결정론적 NFA 전이 단위) | **Accepted** |
| **C6 — 정규식 엔진** | **E-A 자체 매처 (Thompson NFA 시뮬레이션)** 확정 (신규 의존성 0) | **Accepted** |
| **C2 — `maxLength` 상한** | `kMaxMatchStringBytes = 65536` (64 KB) | **Accepted** |
| **C3 — 교대 분기 곱 상한** | `kMaxAlternationProduct = 256` | **Accepted** |
| **정적 패턴 바이트 상한** | `kMaxPatternBytes = 256` | **Accepted** |
| **수량자 상한** | `kMaxQuantifierBound = 1024` (`{n,m}` 에서 $n \le m \le 1024$) | **Accepted** |
| **오류 매핑** | 컴파일 실패 `Errc::SchemaCompileFailed`, 런타임 위반 `Errc::SchemaViolation`, 예산 소진 `Errc::PatternBudgetExhausted` | **Accepted** |
| **ADR 배정** | ADR-0006 (`0006-schema-dialect.md`) | **Accepted** |
