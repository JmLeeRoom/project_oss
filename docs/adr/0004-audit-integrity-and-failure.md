# ADR-0004: CCJ v1 정규 직렬화, LP Digest, 감사 체인 무결성

- **상태**: Proposed
- **날짜**: 2026-08-21
- **결정권자**: 아키텍트, 안전 책임자
- **관련 G0**: G0-23(CCJ 지수), G0-05(operation digest), G0-26(도메인 태그·projection)
- **관련 명세**: `Cogito++_구현명세서.md` §3-1, §3-1-a, §6-1, §7-4, §7-5
- **근거 보고서**: `docs/g0/G0-RESOLUTION-9.md` ② ④ ⑦
- **되돌림 가능성**: **불가.** 이 결정이 바뀌면 기존 감사 체인 전체가 재검증 불가가 되고,
  저장된 승인 기록이 무효화되며, 골든 벡터 24개를 전부 다시 만들어야 한다. **먼저 승인하라.**

> 파일명은 체크리스트 §13 의 `0004-audit-integrity-and-failure.md` 를 따른다.
> 범위에 CCJ/digest 를 포함하는 이유: 감사 해시체인이 CCJ 와 LP 인코딩의 **첫 번째 소비자**이며,
> 둘을 분리하면 같은 인코딩이 두 ADR 에 중복 정의된다.

---

## 맥락

Cogito++ 의 감사 가능성은 세 층으로 성립한다.

```
CCJ v1 (바이트 확정 직렬화)  ->  LP digest (충돌·교차위조 방지)  ->  해시체인 (순서·훼손 탐지)
```

아래층이 흔들리면 위층이 전부 무의미하다. 그런데 아래 두 층에 모순과 공백이 있었다.

**(1) CCJ 숫자 규칙이 자기모순이었다.** §3-1 CCJ-5 는 "지수 자릿수의 선행 0 제거(예: `1.5e-7`)"라고
서술했으나 §3-1-a 규범 골든표는 `1e-07` 을 기재했다.

**(2) 골든표 자체에도 오류가 있었다** — G0-23 이 보고하지 않은 것이다.
`9007199254740994.0`(2^53+2) 행이 `9.007199254740994e+15` 로 기재되어 있으나,
CCJ-5 알고리즘은 그 값을 **생성하지 않는다.**

**(3) `indeterminate` 잠금 키가 턴을 넘어 유지되지 않았다.** §6-4 는 `action_digest` 를 키로 쓰는데
그 digest 는 `session_id·turn_id·action_id` 를 포함한다. 다음 턴의 같은 제안은 다른 키가 되어
안전 잠금이 **한 턴만 살고 사라진다.**

**(4) 9개 digest 중 3개만 계산식이 있었다.** `policy·registry·toolschema·config·model` 의
projection 이 없어, 구현하면 map 순회 순서·enum 숫자값·handler 주소가 digest 에 새어 든다.

---

## 결정

### D1. CCJ v1 숫자 규칙 — 골든표가 권위, 서술 규칙을 정정

```
- NaN / Infinity 는 스키마 단계에서 거부되어 여기 도달하지 않는다.
- 정수값이고 |v| <= 2^53 이면 십진 정수로 출력한다. -0.0 은 "0".
- 그 외에는 C 로케일에서 %.15g -> %.16g -> %.17g 순으로 출력하고,
  strtod 왕복이 원값과 비트 동일한 첫 결과를 채택한다(shortest round-trip).
- 지수 표기는 C99 %g 규칙을 그대로 따른다: 소문자 'e', 부호 필수,
  지수는 최소 두 자리이며 필요하면 그 이상.  후처리로 선행 0 을 제거하지 않는다.
- %g 는 -4 <= X < P 일 때 %f 스타일을 쓴다. 따라서 2^53 을 넘는 정수도
  정밀도에 따라 지수형이 아닌 정수 표기가 될 수 있다. 이는 의도된 동작이다.
```

**근거** — C 표준 fprintf 규격(C99 §7.19.6.1 / C11·C17 §7.21.6.1) 은 `%g`/`%e` 의 지수를 "항상 최소 두 자리"로 규정한다.
`1e-7` 은 `%g` 가 만들 수 없는 형태이며 별도 후처리를 요구한다.
"선행 0 제거"는 ECMAScript/RFC 8785 의 규칙이지 C 의 규칙이 아니다.

### D2. 골든표 2행 정정

| 입력 | 기존 | 정정 |
| --- | --- | --- |
| `1e-7` | `1e-07` | `1e-07` (변경 없음 — 서술 규칙 쪽이 틀렸다) |
| `9007199254740994.0` (2^53+2) | `9.007199254740994e+15` | **`9007199254740994`** |

### D3. 정수 fast-path 는 최적화가 아니라 규범이다

실측 결과 fast-path 와 순수 `%g` 는 `1e15` 에서 갈린다.

```
1000000000000000.0   fast-path -> "1000000000000000"     %g -> "1e+15"
```

**"어차피 %g 가 같은 값을 낸다"는 이유로 fast-path 를 제거하면 digest 가 전부 달라진다.**
2^53 경계는 의미가 있다 — 그 아래에서는 모든 정수가 정확히 표현되므로 정수 표기가 정직하고,
그 위에서는 표현되지 않는 정수가 있으므로 `%g` 의 판단에 맡긴다.

### D4. Locale 독립성 (필수 구현 규범)

`printf("%g")` 는 로케일 의존적이다. 한국어/독일어 로케일에서 소수점이 `,` 가 되면
**모든 digest 가 달라진다.** 전역 로케일은 스레드 간 공유되므로 `setlocale` 은 안전하지 않다.

```
숫자 직렬화는 로케일에 의존해서는 안 된다. 다음 중 하나를 쓴다.
  (a) std::to_chars(first, last, v, std::chars_format::general, P)   ← 정의상 로케일 독립
  (b) 로케일을 명시 고정한 snprintf
      POSIX  — newlocale/uselocale 또는 snprintf_l
      MSVC   — _create_locale(LC_NUMERIC, "C") + _snprintf_s_l
전역 setlocale() 에 의존하는 구현을 금지한다.
```

> ⚠️ **(a)와 (b)가 동일 바이트를 내는지는 아직 검증되지 않았다.**
> `std::to_chars(general)` 의 지수 자릿수 규약이 `printf %g` 와 같은지 표준 문언만으로
> 단정할 수 없다. **S1 Exit Gate 의 3-컴파일러 바이트 비교로 판정한다.**
> 불일치하면 (b)를 규범으로 고정하고 (a)를 금지한다. 판정 전에는 어느 쪽도 확정으로 표기하지 않는다.

**MSVC 이력** — VS2015 이전 MSVC 는 지수를 세 자리로 출력했다(`1e-007`).
지원 최소 버전(MSVC 2019 16.11+)은 C99 준수이나, `SelfTest()` 가 이 회귀를 잡는 것이 존재 이유다.

### D5. LP 인코딩 (명문화)

```
H(f₁ … fₙ) = SHA-256( Σᵢ [ u32le(len(fᵢ)) ‖ fᵢ ] )

  - 첫 필드는 반드시 도메인 태그
  - 문자열/바이트 : UTF-8 원문 바이트
  - 정수          : u64le 고정 8바이트 (십진 문자열 금지)
  - Digest        : 원시 32바이트 (hex 문자열 금지)
```

길이 접두사가 있으므로 빈 문자열과 필드 부재가 구분된다. **그래서 optional 필드도 항상 넣는다.**

### D6. 도메인 태그 9개 — 재사용 금지

| 상수 | 값 | 대상 |
| --- | --- | --- |
| `kAction` | `cogito-action-v1` | Action 고유 (승인 결합) |
| `kOperation` | `cogito-operation-v1` | **신설.** 도구+인자, 턴 무관 |
| `kPermit` | `cogito-permit-v1` | 실행 허가 범위 |
| `kAudit` | `cogito-audit-v1` | 감사 체인 링크 |
| `kPolicy` | `cogito-policy-v1` | 정책 스냅샷 |
| `kRegistry` | `cogito-registry-v1` | 레지스트리 스냅샷 |
| `kToolSchema` | `cogito-toolschema-v1` | 개별 도구 스키마 |
| `kConfig` | `cogito-config-v1` | 설정 스냅샷 |
| `kModel` | `cogito-model-v1` | 모델·가중치·템플릿 |

서로 다른 대상이 같은 태그를 쓰면 **교차 위조**가 가능하다.

### D7. `operation_digest` 신설 — 잠금 키와 멱등 키의 분리

```
operation_digest = SHA-256( LP("cogito-operation-v1") || LP(tool_name) || LP(CCJ(arguments)) )
```

| digest | 수명 | 용도 |
| --- | --- | --- |
| `action_digest` | 한 Action | 승인 결합(불변식 5), `idempotency_key = lowercase_hex(action_digest)` |
| `operation_digest` | 세션 전체 | `indeterminate` 잠금, 동일 작업 반복 판정 |

`idempotency_key` 를 `operation_digest` 로 바꾸지 않는 이유: 불변식 9 가 자동 재시도를 금지하므로
다음 턴의 재제안은 *새 작업*이다. 같은 키를 외부 시스템에 주면 정당한 재조작이 중복으로 무시된다.

### D8. Projection 표 — 무엇을 넣고 무엇을 넣지 않는가

전문은 `docs/g0/G0-RESOLUTION-9.md` ⑦ 의 표를 규범으로 삼는다. 핵심 3규칙만 여기 고정한다.

1. **enum 은 숫자가 아니라 고정 소문자 문자열**(`"write"`, `"high"`, `"full"`).
   숫자값은 enum 재정렬 시 조용히 바뀐다.
2. **누락 optional 필드는 빈 문자열 `""` 로 통일**하고 항상 넣는다.
   생략/`null`/`""` 를 섞으면 같은 의미가 다른 digest 를 낸다.
3. **정렬 기준을 명시**한다. registry 는 name 오름차순, policy 는 (priority 내림, rule_id 오름).
   **handler 주소·객체 주소·map 삽입 순서는 절대 포함하지 않는다.**

### D9. 감사 체인과 실패 정책 (§7-4·§6-3 재확인)

- `prev_hash` 최초값은 문서화된 32바이트 zero
- `seq`(AUTOINCREMENT)가 전역 순서 권위. **`seq` 자체는 해시 입력에 넣지 않는다**
  (DB 채번값이라 재구성 불가). 순서는 `prev_hash` 체인이 보증한다
- 프로세스 내 순서는 `(process_epoch_id, monotonic_ns)` 사전식.
  `wall_time_utc` 는 표시·상관관계 전용이며 순서 판정에 쓰지 않는다
- append-only 는 WAL 이 아니라 **trigger + `sqlite3_set_authorizer`** 가 강제한다
- 감사 커밋 실패는 fail-closed. write 실행 횟수 0 (불변식 8)
- `turn_end` 커밋 실패 시 `finalize_pending_` → `RetryFinalize()` / `SealSession()`.
  봉인마저 실패하면 프로세스 중단

---

## 대안과 기각 이유

| # | 대안 | 장점 | 기각 이유 |
| --- | --- | --- | --- |
| 1 | RFC 8785(JCS) 전면 채택 | 외부 상호운용 | 숫자 규칙이 ECMAScript `Number::toString` 을 요구. 표준 C++ 로 정확히 재현하려면 별도 dtoa 필요. FP `to_chars` 가용성도 균일하지 않다 |
| 2 | 서술 규칙(`1e-7`)을 권위로, 골든표를 고침 | 8785 에 가까워짐 | `%g` 출력에 후처리가 필요해지고, 후처리 자체가 새 버그 표면이 된다. 이득 없음 |
| 3 | `action_digest` 에서 session/turn 을 제거해 잠금 키로 재사용 | 태그 1개 절약 | 승인 결합(불변식 5)이 깨진다. **안전상 채택 불가** |
| 4 | 잠금 키를 `tool_name + CCJ(args)` 원문으로 | digest 불필요 | 키가 무한정 길어지고 감사 payload 에 인자 원문이 남는다(§7-5 위반) |
| 5 | Canonical CBOR 로 통일 | 이진 형식이 간결 | 감사 payload 를 사람이 읽을 수 없게 된다. SQLite `payload_json` 조회가 불가능 |
| 6 | `seq` 를 해시 입력에 포함 | 순서가 해시로 고정 | DB 가 채번하므로 커밋 전에 알 수 없다. 계산 순서가 뒤집힌다 |

---

## 안전 영향

| 불변식 | 영향 |
| --- | --- |
| **5** (승인 결합) | D7 이 `action_digest` 의 턴 고유성을 보존한다. 대안 3 을 택하면 위조 가능 |
| **9** (indeterminate) | D7 이 잠금을 턴 너머로 유지시킨다. **이 결정 없이는 §6-4 안전 통제가 실효 없다** |
| **7·8** (감사 선행·실패 시 차단) | D9 |
| **10** (외부 데이터 불신) | D8 규칙 3 이 handler 주소 등 내부 상태 유출을 막는다. D4 가 로케일 주입을 막는다 |

**추가 위협 — 로케일 주입.** 공격자가 프로세스 로케일을 바꿀 수 있으면
D4 없이는 모든 digest 를 바꿔 감사 체인 검증을 실패시킬 수 있다(가용성 공격).
D4 는 안전 요구사항이지 이식성 요구사항이 아니다.

---

## 롤백·마이그레이션 영향

**이 ADR 이 바뀌면:**

- 기존 `audit.db` 의 체인 검증이 **전부 실패**한다. 마이그레이션 경로가 없다
- 저장된 `ApprovalRecord` 의 digest 가 맞지 않아 **승인 이력이 검증 불가**가 된다
- 골든 벡터 24개 + digest 고정 벡터 9세트를 전부 재생성해야 한다
- 외부에 앵커링한 체인 헤드 해시가 무효가 된다

**그래서 S1 착수 전에 승인해야 한다.** 코드가 굳은 뒤에는 되돌릴 수 없다.

- **Codex 인계**: `src/canonical_json.cpp`(D1·D3·D4), `src/digest.cpp`(D5·D6·D7·D8),
  `src/audit_sqlite/hash_chain.cpp`(D9). D4 의 3-컴파일러 비교 결과를 보고할 것

---

## 검증 방법

| 테스트 | 확인 |
| --- | --- |
| `canonical/ccj_golden` | 정정된 24 벡터 바이트 일치. **MSVC/GCC/Clang 산출 파일의 SHA-256 직접 비교** |
| `canonical/ccj_locale` | 로케일을 `ko_KR.UTF-8` / `de_DE.UTF-8` / `C` 로 바꿔도 출력 동일 |
| `canonical/ccj_roundtrip_property` | 무작위 double 20만 개 이상, 출력을 재파싱하면 원래 비트 |
| `canonical/ccj_fastpath_boundary` | `1e15`, `2^53`, `2^53+2`, `1e17` 경계에서 정수형/지수형 판정 고정 |
| `canonical/digest_vectors` | 9개 digest 각각의 고정 벡터 |
| `canonical/digest_mutation` | 각 projection 필드를 하나씩 바꾸면 digest 가 달라짐 (parameterized) |
| `canonical/digest_domain_separation` | 같은 입력, 다른 태그 → 다른 digest |
| `canonical/lp_ambiguity` | 빈 문자열 필드와 필드 부재가 다른 digest 를 냄 |
| `core/registry.digest_order_independence` | 등록 순서를 바꿔도 `registry_digest` 동일 |
| `core/policy.digest_file_order_independence` | 파일 내 규칙 순서를 바꿔도 `policy_digest` 동일 |
| `audit/chain.*` | 필드별 변조 탐지, 중간 행 삭제, 순서 변경, head 변경 |
| `audit/epoch` | 프로세스 재시작 경계에서 monotonic 검사 분리 |

**S1 Exit Gate** — 위 `canonical/*` 가 **3개 컴파일러에서 바이트 동일**해야 이 ADR 이 `Accepted` 가 된다.

---

## 미결

- **D4 (a)/(b) 중 무엇을 규범으로 할지** — 3-컴파일러 비교 결과가 나온 뒤 이 ADR 을 갱신한다
- **`config_digest` 의 `*_ref` 처리** — 참조 문자열(`env:NAME`)만 넣기로 했으나,
  같은 참조가 다른 값을 가리키면 digest 가 같아진다. 비밀값을 넣을 수는 없으므로
  "설정 파일의 형태가 같으면 같은 digest" 라는 의미로 한정함을 문서에 명시할 것
- **`Observe` 에서 `tool_result` 커밋이 실패한 경우** — 이미 설비 write 는 일어났는데
  턴이 `Failed` 로 끝난다. 감사에 "성공한 write 가 있으나 결과 기록 실패" 상태를
  어떻게 남길지 별도 규범 필요 (ADR-0001 미결과 동일 사안)
