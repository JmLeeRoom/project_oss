# G0-18 · G0-19 · G0-20 정정 보고서 — 웹 계약 3건

| | |
| --- | --- |
| **대상** | G0-18(세션·CSRF) · G0-19(body schema·오류 envelope·멱등 캐시) · G0-20(`assets_digest`) |
| **작성** | Claude (계약 관리자 · 감사관) |
| **기준일** | 2026-08-24 |
| **원본** | `Cogito++_구현명세서.md` §12-5·§12-6·§12-7·§12-9·§12-10·§12-11·§12-13·§12-14, `Cogito++_개발_작업체크리스트.md:90`~`:92` |
| **상태** | **Proposed — 사람 승인 대기.** 승인 전에는 명세 본문을 고치지 않는다 |
| **관련 ADR** | `0009-web-trust-boundary`(미작성) · `0001-fsm-turn-and-action`(Proposed) · `0004-audit-integrity-and-failure`(Proposed) |
| **제외** | **G0-17 은 이 문서의 범위가 아니다.** 인증원·역할 체계·SOD 예외는 현장값이며 AI 가 정할 수 없다 (`docs/g0/G0-RESOLUTION-9.md:697-698`) |

> **읽는 법** — 각 항목은 `[모순] → [선택지] → [확정] → [명세 수정 diff] → [영향]` 순이다.
> **되돌림 불가** 표시가 있는 결정은 나중에 바꾸면 기존 감사 데이터·빌드 산출물·승인 기록이 무효가 된다.

---

## 선반영 고지 (필독)

이 문서는 **아직 승인되지 않은 결정 4건을 선반영**했다. 승인 전에는 규범이 아니다.

| 선반영한 것 | 출처 | 상태 | 이 문서에서 쓰인 곳 |
| --- | --- | --- | --- |
| ABI **v1.1 단일 기준**, MINOR 증가 규칙(구조체 끝 필드 추가 + `struct_size` 보호) | `G0-RESOLUTION-9.md:51-65` | Proposed | D20-4 (`cogito_config_t` 필드 추가) |
| 요청별 `cogito_subject_t` · 승인자는 Subject | `Cogito++_구현명세서.md:2160-2194` (§8-4 [S-1][S-2]) | 명세 본문 = 현재 최고 권위 | D18-3 · D19-1 |
| **LP 인코딩 규약**(`u32le(len)‖bytes`, 정수 u64le, digest 원시 32바이트) | `G0-RESOLUTION-9.md:459-466` | Proposed · **되돌림 불가** | D20-1 (`assets_digest` 집계식) |
| **도메인 태그 9개 고정** | `G0-RESOLUTION-9.md:441-455` | Proposed · **되돌림 불가** | D20-1 (`cogito-assets-v1` 을 **10번째 태그로 만들지 않는** 근거) |
| `operation_digest` 를 `indeterminate` 잠금 키로 사용 | `G0-RESOLUTION-9.md:130-148` | Proposed · **되돌림 불가** | D19-1 (`POST /api/indeterminate/ack` body) |

**현재 최고 권위는 `Cogito++_구현명세서.md` 다** (`docs/adr/0001-fsm-turn-and-action.md:3`·`docs/adr/0004-audit-integrity-and-failure.md:3`·`docs/g0/G0-RESOLUTION-9.md:9` 가 모두 `Proposed`).
위 4건 중 하나라도 승인 과정에서 뒤집히면 **D20-1 과 D20-4 를 다시 써야 한다.**

---

## 0. 요약

| # | 항목 | 확정 결정 | 되돌림 |
| --- | --- | --- | --- |
| ① | **G0-18** | 세션 = **서버 측 상태 + 불투명 쿠키**(`HttpOnly`·`Secure`·`SameSite=Strict`·host-only·`Max-Age` 없음). CSRF = **Origin 정확일치 + synchronizer token + content-type** 3중 AND. 토큰은 `GET /api/state` 로 전달하고 **로그인·step-up·역할 변경 시에만 회전**. 로그아웃은 웹 세션만 파기하고 턴·승인에 손대지 않는다 | 가능 |
| ② | **G0-19** | POST 7경로 필수 body 확정(`command_id` UUIDv4 공통). 오류 envelope 은 `error_code`(전송계층)와 `reason_code`(§3-4 Gate 판정)를 **별도 축**으로 분리. 멱등 캐시 키 = `(subject_id, command_id)`, 용량 256, **FIFO 퇴거**, 메모리 전용 | 가능 |
| ③ | **G0-20** | `assets_digest` = `dist/` 전 파일의 **압축 전 원본 바이트** 트리 해시. 빌드 시 산출 → 기동 시 **재계산 대조** → 불일치 시 **바인드 이전에 프로세스 종료**. 기대값은 config 파일이 아니라 **컴파일타임 상수** | **불가** (빌드 파이프라인·`dist.sha256` 형식이 이 정의에 묶인다) |

**③이 되돌림 불가인 이유** — 집계식을 바꾸면 이미 반입된 매체의 `dist.sha256` 과 이미 빌드된 `assets_embedded.cpp` 의 기대값이 전부 무효가 된다. 폐쇄망 반입은 물리 매체 재반입을 뜻한다(`Cogito++_구현명세서.md:2953`).

---

## 실측 기준선 (2026-08-24, 워킹트리)

정정안이 "무엇을 고치는지" 를 고정하기 위해 현재 산출물을 직접 측정했다. **아래는 추정이 아니라 실행한 grep 의 출력이다.**

| # | 측정 | 결과 |
| --- | --- | --- |
| M1 | `tools/mock_server/src/server.js` 의 POST 경로 | 7개 — `:300 /api/turn` · `:346 /api/approve` · `:396 /api/reject` · `:434 /api/cancel` · `:465 /api/indeterminate/ack` · `:489 /api/finalize/retry` · `:495 /api/session/seal`. §12-5(`:2762-2768`) 7개와 **경로명 일치** |
| M2 | `command_id` 수신 | **1/7.** `:308` 의 `/api/turn` 만. `commandCache` 참조도 `:28`·`:312`·`:313`·`:332` 4곳 전부 `/api/turn` 블록 안 |
| M3 | CSRF 검증 | **0건.** `X-Cogito-CSRF` 문자열은 `:214` 의 `Access-Control-Allow-Headers` 응답 헤더에만 등장하고, 어디서도 **읽히지 않는다** |
| M4 | Origin 검사 | `:202` `origin.includes('127.0.0.1') \|\| origin.includes('localhost')` — **부분일치**이며 `!origin`(헤더 부재)을 허용으로 취급한다. 결과값 `isAllowedOrigin` 은 `:211` **CORS 반사에만** 쓰이고 POST 거부에는 쓰이지 않는다 |
| M5 | content-type 강제 | **0건.** 요청 `Content-Type` 을 읽는 코드 없음 |
| M6 | 멱등 캐시 퇴거 | **없음.** `commandCache` 는 `Map` 이고 `delete`·크기 검사가 0회. `:332` 에 `timestamp` 를 저장하지만 **어디서도 읽지 않는다** → TTL 없음, 용량 상한 없음 |
| M7 | 오류 envelope | `{ error: "<자유 영문 문장>" }` 단일 필드. `:236`·`:304`·`:321`·`:350`·`:359`·`:368`·`:376`·`:400`·`:409`·`:416`·`:637`. `reason_code` 없음, `command_id` 없음 |
| M8 | 성공 응답 모양 | 서로 다름 — `:437 {status:'ok'}` · `:454 {state:'Cancelled'}` · `:491 {status:'retry_initiated'}` · `:497 {status:'session_sealed'}` · `:385 {approval_id, state:'approved'}`. `command_id` 를 되돌려주는 응답 **0건** |
| M9 | 명세에 없는 `reason_code` | `:447 'user_cancelled'` · `:473 'lockdown_cleared'` — 둘 다 `Cogito++_구현명세서.md` 전문 grep **0 hit**. §3-4(`:275-284`) 표에 없는 식별자다 |
| M10 | `assets_digest` | `:512` 에 **64자 하드코딩 리터럴**. 어떤 번들에서도 산출되지 않고 검증도 없다. `tools/`·`tests/` 전체에서 이 한 줄이 유일한 등장 |
| M11 | 테스트의 거짓 통과 | `tests/web/csp-and-hmi.test.js:395` 가 `web/csrf: VERIFIED (CSRF headers & Origin check on REST)` 를 출력한다. M3·M4 와 정면 충돌 |

> **M2·M4 는 `docs/STATUS-AUDIT-2026-08-24.md:241-242`(G6·G7) 의 판정보다 완화된 상태다.**
> 그 문서 작성 이후 워킹트리가 움직였다 — `/api/turn` 한 경로에 `command_id` 와 부분일치 Origin 검사가 들어왔다.
> **그러나 방어로는 성립하지 않는다**: Origin 값이 거부에 쓰이지 않고(M4), `https://127.0.0.1.attacker.example` 이 부분일치를 통과한다.
> 이 문서는 STATUS-AUDIT 의 문장을 인용하지 않고 **위 실측을 근거로 삼는다.**

---

## ① G0-18 — 로그인 · 세션 쿠키 · CSRF · bootstrap

### 모순

| # | 위치 | 서술 | 문제 |
| --- | --- | --- | --- |
| a | `구현명세서:2805` | `"session_ttl_ms": 3600000` | 세션을 **만드는** 경로가 §12-5 15개(`:2760-2781`)에 없다 |
| b | `구현명세서:2809` | `"csrf_header": "X-Cogito-CSRF"` | 헤더 **이름만** 있다. 발급·전달·회전·검증·바인딩 규칙 전부 없음 |
| c | `구현명세서:2806-2807` | `step_up.required_for` 5개 + `ttl_ms: 120000` | step-up 을 **승격시키는** 경로가 없다. 승격 수단이 없으면 W11(`:2645`)이 요구한 승인류 5경로가 영구 거부된다 |
| d | `구현명세서:3012` | `web/auth`: *"미인증 요청은 **정적 자산 포함** 전부 거부"* | 정적 자산까지 거부하면 **로그인 화면 자체를 로드할 수 없다.** mTLS 모드에서는 TLS 핸드셰이크가 인증이라 모순이 없지만, badge·oidc 모드에서는 부트스트랩이 불가능하다 |
| e | 명세 전문 | `Set-Cookie`·`HttpOnly`·`SameSite` **0 hit** | 세션을 쿠키로 나른다는 서술 자체가 없다. 그런데 `session_ttl_ms` 는 있다 |
| f | `구현명세서:1752` / `:2750` / `:2805` | `session` 이 **세 가지**를 가리킨다 | (a) config 블록 `session:{max_turns, approval_timeout_ms}` (b) 라인 = FSM `session_id` (c) HTTP 인증 세션. `POST /api/session/seal`(`:2768`)은 (b)를 봉인한다 |

**(f)가 가장 위험하다.** 세 의미가 한 낱말을 공유하는 상태에서 "세션 만료" 를 구현하면, 인증 세션 만료가 라인 세션 봉인으로 번역될 수 있다. 그것은 W14(`:2648`)가 명시적으로 금지한 실패 — *"연결 종료·타임아웃을 취소로 해석하는 것을 금지한다"* — 와 같은 종류다.

### 선택지

**CSRF 방어 방식**

| # | 방식 | 판정 |
| --- | --- | --- |
| 1 | **double-submit cookie** | 서버가 토큰을 기억하지 않으므로 **W11 step-up 상태와 결합할 수 없다.** 쿠키를 심을 수 있는 위치가 하나라도 있으면(동일 사이트 다른 호스트, 평문 구간) 우회된다. **기각** |
| 2 | **synchronizer token** (서버 세션 바인딩) | 세션 무효화 시 토큰도 함께 죽는다. step-up 승격 상태를 같은 저장소에 둘 수 있다. **채택** |
| 3 | Origin 정확일치 **단독** | W10(`:2644`)이 *"인증 + Origin/Sec-Fetch-Site 검증 + CSRF 토큰 + `application/json` 강제를 **모두** 통과"* 로 못박았다. 단독 채택은 명세 위반. **기각** |

**세션 만료 방식**

| # | 방식 | 판정 |
| --- | --- | --- |
| 1 | 절대 만료만 | 채택 (아래) |
| 2 | 유휴(idle) 만료 추가 | **기각.** §12-7(`:2853`)의 15초 keepalive 가 상시 트래픽을 만들어 유휴 판정이 무의미하다. 도입하면 "탭이 열려 있으면 영원히 갱신" 이 되어 사실상 무제한 세션이 된다 |
| 3 | 쿠키 `Max-Age` 로 만료 | **기각.** 브라우저 시계를 신뢰하게 된다. §12-8 [A-6](`:2878-2879`)이 같은 이유로 `Date.now` 계산을 금지했다 |

### 확정

#### D18-1. 세션 표현 — 불투명 쿠키 + 서버 측 상태

인증원(G0-17)이 무엇으로 정해지든 **세션의 표현 방식은 동일하다.** 인증원은 "누구인가" 를 정하고, 이 절은 "그 판정을 어떻게 이후 요청에 나르는가" 를 정한다.

```
Set-Cookie: cogito_session=<불투명 128비트 이상 CSPRNG 값, base64url>;
            HttpOnly; Secure; SameSite=Strict; Path=/
```

| 속성 | 값 | 근거 |
| --- | --- | --- |
| 값 | **불투명**. subject_id·역할·만료시각을 담지 않는다 | W5(`:2639`) — 신원은 서버가 보관한다. 클라이언트가 나르는 값에 신원을 넣으면 검증 대상이 늘어난다 |
| `HttpOnly` | **필수** | §12-10(`:2915-2938`)이 닫는 것은 XSS **주입**이고, `HttpOnly` 는 주입이 성공했을 때의 **탈취**를 막는다. 서로 다른 계층 |
| `Secure` | **필수. 조건부 완화 없음** | §12-6(`:2800`) `tls.enabled: true`, `:2837` `upgrade-insecure-requests` |
| `SameSite` | **`Strict`** | 대시보드는 외부 사이트에서 링크로 진입할 필요가 없다(§12-6 `:2808` 단일 출처 allowlist, §12-12 `:3004` OT/IT 경계). `Lax` 대비 잃는 것이 없다. **단 이것은 3중 방어의 보조축이다** — 브라우저 기준선이 §12-12(`:2998`)에서 미확정이므로 `SameSite` 를 단독 방어로 삼지 않는다 |
| `Domain` | **설정하지 않는다** (host-only) | 서브도메인으로 쿠키가 새는 경로를 만들지 않는다 |
| `Max-Age`/`Expires` | **넣지 않는다** (세션 쿠키) | 만료 권위는 서버 하나. 위 선택지 3 참조 |
| `Path` | `/` | |

- **쿠키 접두사** — `__Host-` 접두사를 붙이면 브라우저가 `Secure`·`Path=/`·`Domain` 부재를 강제한다고 알려져 있으나, **1차 자료(RFC 6265bis) 를 대조하지 않았다. 「미확인」으로 남긴다.** 확인 후 `__Host-cogito_session` 으로 확정할 것을 권고한다.
- **만료 판정은 서버 monotonic 기준**이다. 절대 만료 하나만 둔다: `auth.session_ttl_ms`(§12-6 `:2805`).
- **재시작 시 전 세션 무효.** 세션 저장소는 메모리 전용이다. `process_epoch_id`(§7-4 `:1902`)가 바뀌면 클라이언트는 §12-7(`:2852`)에 따라 어차피 전체 상태를 다시 조회해야 한다.

#### D18-2. CSRF 방어 — 3중 AND. 하나라도 실패하면 거부하고 write 0회

W10(`:2644`)이 요구한 4요소 중 인증을 제외한 3개의 **판정 규칙**을 확정한다.

**(a) Origin — 정확 일치**

```
① Origin 헤더 부재                      -> 403 (거부)
② Origin != origin_allowlist 의 원소     -> 403
   비교는 스킴·호스트·포트를 포함한 문자열 정확 일치.
   부분일치 · 접미사 일치 · includes() · 정규식 금지.
③ Sec-Fetch-Site 가 있고 != "same-origin" -> 403
④ Sec-Fetch-Site 부재                    -> 통과 (거부하지 않는다)
```

- ①이 거부인 근거: §12-13 `web/csrf`(`:3013`)가 *"Origin 없음"* 을 **거부 케이스로 명시**했다.
- ③과 ④의 비대칭이 의도적인 이유: 브라우저 기준선이 §12-12(`:2998`)에서 미확정이라 `Sec-Fetch-Site` 를 보내지 않는 브라우저를 배제할 근거가 없다. **있으면 강하게 쓰고, 없으면 다른 두 축이 남는다.**
- 실측 M4 가 정확히 이 규칙의 반례다. `origin.includes('127.0.0.1')` 은 `https://127.0.0.1.attacker.example` 을 통과시킨다.

**(b) CSRF 토큰 — synchronizer token**

| 항목 | 확정 |
| --- | --- |
| 생성 | 세션 생성 시 1회. CSPRNG 128비트 이상. **세션당 유효 토큰 1개** |
| 저장 | 서버 세션 레코드 안. 쿠키로 내려보내지 **않는다**(double-submit 이 아니다) |
| 전달 | **`GET /api/state` 응답 본문의 `csrf_token` 필드.** 새 경로를 만들지 않는다 |
| 제출 | 요청 헤더 `X-Cogito-CSRF` (§12-6 `:2809` 값 그대로) |
| 검증 | 상수시간 비교. 부재·불일치 → 403 |
| 회전 | **① 로그인 성공 직후 ② step-up 승격 성공 직후 ③ 역할(roles) 변경 시.** 이 3개뿐 |
| 수명 | 세션과 동일. 별도 TTL 없음 |
| 결합 | 웹 세션 + `subject_id`. 세션 무효화 시 즉시 무효 |

- **전달 경로로 `GET /api/state` 를 고른 이유** — §12-5(`:2774`)에서 이미 "인증된 모든 역할" 에 열려 있고, 인증 직후 대시보드가 반드시 처음 호출하는 경로다. 전용 경로를 새로 만들면 §12-5 의 경로 수가 늘어나는데, 그 비용을 치를 이유가 없다. `Cache-Control: no-store`(`:2841`)가 이미 모든 `/api/*` 에 걸려 있어 캐시 유출 경로도 없다.
- **요청마다 회전하지 않는 이유** — SSE 재연결(§12-7 `:2851`)과 다중 탭에서 토큰 경합이 생긴다. 운영자가 승인 화면에서 토큰 불일치로 튕기면 그것이 곧 재시도 압력이 되고, W13(`:2647`)이 지목한 *"운영자가 재승인을 시도하고, 그것이 이중 재개를 만든다"* 와 **같은 실패 유형**이다.

**(c) content-type — 정확 일치**

```
Content-Type: application/json            -> 통과
Content-Type: application/json; charset=utf-8 -> 통과 (charset 파라미터만 허용)
그 외 (text/plain · multipart/form-data · application/x-www-form-urlencoded · 부재) -> 415
```

§12-13 `web/csrf`(`:3013`)가 `text/plain` 본문을 거부 케이스로 명시했다. 이 셋은 브라우저가 **CORS preflight 없이 보낼 수 있는 형식**이며, 강제의 목적은 W10(`:2644`)이 적은 대로 *"simple request 로 위조 불가능하게"* 하는 것이다.

> **과대주장 금지** — 이 3중 검사는 **브라우저를 경유한 교차 출처 위조**를 막는다. 브라우저를 경유하지 않는 직접 HTTP 요청은 세션 쿠키를 얻어야 하므로 **인증 계층이 막고**, 이 절이 막는 것이 아니다. 그리고 §12-10 이 다루는 동일 출처 XSS 가 성립하면 세 축이 모두 정상 경로로 통과한다 — 그 잔여 위험은 §12-10(`:2921-2937`)과 §12-8 [A-8](`:2882`)이 닫는다.

#### D18-3. 로그아웃 · 세션 만료 — 턴과 승인에 손대지 않는다

```
POST /api/auth/logout 이 하는 일:
  1. 서버 세션 레코드 파기
  2. CSRF 토큰 파기
  3. step-up 승격 상태 파기
  4. 해당 세션이 열어둔 SSE 스트림 종료
  5. Set-Cookie 로 쿠키 만료

POST /api/auth/logout 이 절대 하지 않는 일 (명문 금지):
  ✗ cogito_cancel_turn / cogito_request_cancel 호출
  ✗ cogito_seal_session 호출
  ✗ 대기 중 승인(pending approval) 취소·만료
  ✗ 진행 중 턴에 대한 어떤 영향
```

- 근거: W14(`:2648`) *"턴의 수명은 어떤 연결의 수명과도 독립이다"*, §12-12(`:3001`) *"대기 중 승인은 운영자 로그아웃·세션 만료 후에도 소멸하지 않으며, 다음 교대자가 `GET /api/approvals/pending` 으로 본다. 승인 만료는 시간으로만 발생한다."*
- **명문 금지로 두는 이유** — W14 가 경고한 `res.onAborted → cancel_turn` 과 정확히 같은 실패 패턴이다. "로그아웃하면 정리한다" 는 상식적인 코드가 `Execute` 중이면 §6-4 에 따라 설비를 `indeterminate` 로 만든다.
- 세션 만료(`session_ttl_ms` 경과)도 **동일한 규칙**을 따른다. 만료는 로그아웃과 같은 5단계를 수행하고, 금지 4항목도 같다.
- step-up 만료(`step_up.ttl_ms`, `:2807` = 120000)는 세션 만료와 **독립**이다. 만료되면 세션은 살아 있고 step-up 필요 경로만 403 `step_up_required` 를 받는다.

**step-up 대상 경로 명칭 사상** — §12-6 `required_for`(`:2806`)와 §12-5 경로명(`:2762-2768`)의 표기가 다르다. 개수는 **5개로 일치**하므로 모순이 아니라 표기 문제다. 사상표를 계약으로 고정한다.

| `required_for` 원소 | §12-5 경로 |
| --- | --- |
| `approve` | `POST /api/approve` |
| `reject` | `POST /api/reject` |
| `seal` | `POST /api/session/seal` |
| `ack` | `POST /api/indeterminate/ack` |
| `finalize_retry` | `POST /api/finalize/retry` |

`POST /api/turn` 과 `POST /api/cancel` 은 step-up 대상이 **아니다**(§12-5 `:2762`·`:2765` 의 `—`).

#### D18-4. 부트스트랩 경로 — 2개 확정, 1개는 G0-17 의존

| 경로 | 상태 | 근거 |
| --- | --- | --- |
| `POST /api/auth/logout` | **확정 · 인증원 무관하게 필요** | 교대 인수인계(§12-12 `:3001`)에서 이전 근무자 세션을 끊지 못하면 W12 자기승인 분리(`:2646`)와 §12-4(`:2752`)의 귀속이 성립하지 않는다. mTLS 라도 브라우저가 인증서를 캐시하므로 **서버 측 세션 파기가 별도로 필요**하다 |
| `POST /api/auth/step-up` | **확정 · 인증원 무관하게 필요** | W11(`:2645`)이 승인류 5경로에 step-up 을 요구하는데 승격 경로가 없으면 그 5개가 영구 거부된다. **승격 수단**(badge/pin/mtls, `:2807`)은 G0-17 의존이지만 **경로의 존재**는 의존하지 않는다 |
| `POST /api/auth/login` | **G0-17 의존 → ADR 0009** | mTLS 모드에서는 TLS 핸드셰이크가 곧 인증이므로 이 경로가 **없을 수도 있다**. badge·oidc 모드에서는 필요하다 |

- **접두사를 `/api/auth/` 로 정한 이유** — `/api/session/seal`(`:2768`)이 이미 **라인 세션**(§12-4 `:2750`)을 뜻하므로, 인증 세션에 같은 접두사를 쓰면 모순 (f)를 API 표면에 영구히 고정시킨다.
- **§12-5 의 경로 수는 15개가 아니라 17~18개가 된다** (POST 9~10 + GET 8). 이 숫자 변화를 §12-5 본문과 §12-0(`:2629`, *"§12-5의 15개 경로로 대체"*) 양쪽에 반영해야 한다.
- 모순 (d) — *"미인증 요청은 정적 자산 포함 전부 거부"*(`:3012`)의 해석도 인증원에 따라 갈린다. **ADR 0009 로 이관**한다. 다만 **어느 해석이든 `/api/*` 는 예외 없이 인증 필수**라는 점은 확정한다.

#### D18-5. 데모 모드(`insecure_no_auth: true`)

- 세션 쿠키를 **발급하지 않는다.** CSRF 토큰도 없다. 인증이 없으면 세션이 없다.
- 그러나 **Origin 정확 일치와 content-type 강제는 그대로 유지한다** (3중 중 2중).
- 근거: W9(`:2643`)가 `effect != none` 도구를 전부 Deny 하므로 위조 요청이 **설비를 조작할 수는 없다**. 그러나 `POST /api/turn`·`POST /api/cancel` 은 `effect = none` 경로로 남아 있어, 교차 출처 요청이 남의 라인에서 턴을 유발하거나 진행 중 턴을 취소시킬 수 있다. 기동검사 2(`:2822`)에 의해 루프백 전용이므로 위험은 국소적이지만, 두 축을 유지하는 비용이 0 이다.

### 명세 수정 diff

```diff
 ### 12-5. HTTP API 계약

+**인증 세션 (POST — Origin + `application/json` 강제. CSRF 토큰은 세션 수립 이후부터 요구)**
+
+| 경로 | 반환 | step-up | 비고 |
+| --- | --- | --- | --- |
+| `POST /api/auth/logout` | `204` | — | 웹 세션만 파기. **턴·승인·라인 세션에 영향 없음** (W14) |
+| `POST /api/auth/step-up` | `204` | — | `step_up.ttl_ms` 동안 승격. 수단은 ADR 0009 |
+| `POST /api/auth/login` | (ADR 0009) | — | **인증원 미확정.** mTLS 모드에서는 존재하지 않을 수 있다 |
+
 **명령 (POST — 인증 + Origin + CSRF + `application/json` + `command_id` 필수)**
```

```diff
 ### 12-6. 인증·인가·바인드

+**세션 표현 (인증원과 무관)**
+
+```
+Set-Cookie: cogito_session=<불투명 CSPRNG ≥128bit, base64url>;
+            HttpOnly; Secure; SameSite=Strict; Path=/
+```
+- 값은 불투명하다. subject_id·역할·만료시각을 담지 않는다 (W5)
+- `Max-Age`/`Expires` 를 넣지 않는다. 만료 권위는 서버 monotonic + `session_ttl_ms` 하나다
+- `Domain` 을 설정하지 않는다 (host-only)
+- 세션 저장소는 메모리 전용이다. 프로세스 재시작 시 전 세션 무효
+
+**CSRF (synchronizer token) — 3중 AND. 하나라도 실패하면 거부하고 write 0회**
+
+| 축 | 규칙 | 실패 시 |
+| --- | --- | --- |
+| Origin | `origin_allowlist` 원소와 **문자열 정확 일치**. 부분일치·접미사·정규식 금지. **헤더 부재는 거부**. `Sec-Fetch-Site` 는 있으면 `same-origin` 요구, 없으면 통과 | 403 |
+| CSRF 토큰 | 서버 세션에 바인딩. `GET /api/state` 응답 `csrf_token` 으로 전달, `X-Cogito-CSRF` 헤더로 제출, 상수시간 비교. **회전은 로그인·step-up 승격·역할 변경 3회뿐** | 403 |
+| content-type | `application/json` 정확 일치 (`charset` 파라미터만 허용) | 415 |
+
+**로그아웃·세션 만료 — 명문 금지**
+
+로그아웃과 세션 만료는 웹 세션·CSRF 토큰·step-up 상태·SSE 스트림만 파기한다.
+`cogito_cancel_turn`·`cogito_seal_session` 호출, 대기 승인 취소·만료를 **금지한다** (W14, §12-12).
+
+**`insecure_no_auth: true`** — 세션 쿠키와 CSRF 토큰을 발급하지 않는다.
+Origin 정확 일치와 content-type 강제는 유지한다.
+
 **기동 시 검사 — 실패하면 프로세스 시작 실패**
```

### 영향

- **Antigravity 인계** — 실측 M3·M4·M5 해소. `:202` 의 `includes` 부분일치를 정확 일치로, `isAllowedOrigin` 을 CORS 반사가 아니라 **POST 거부**에 연결. `X-Cogito-CSRF` 를 실제로 읽고 검증. 그리고 M11 의 `tests/web/csp-and-hmi.test.js:395` 출력을 실제 구현 상태와 일치시킨다 — **구현 전에는 `VERIFIED` 를 출력하지 않는다.**
- **Codex 인계** — `tools/web_host/` 의 POST 전처리 1곳에서 3축을 모두 판정한다. 경로별 분산 구현 금지 (§12-3 `:2697` `AUTH` 노드가 단일 지점이다).
- **테스트** — §12-13 `web/csrf`(`:3013`)의 4개 케이스에 **CSRF 토큰 회전 후 구토큰 거부**와 **로그아웃 후 턴 계속 진행**을 추가한다. 후자는 `web/lifetime`(`:3018`)과 짝을 이룬다.
- **G0-17 이관 7건** — 아래 §G0-17 의존 목록.

---

## ② G0-19 — body schema · 오류 envelope · 멱등 캐시

### 모순

| # | 위치 | 문제 |
| --- | --- | --- |
| a | `구현명세서:2762-2768` | POST 7개 중 body 를 명시한 것은 **3개뿐**(approve 3필드, reject `reason`, ack `note`). `turn`·`cancel`·`finalize/retry`·`session/seal` 은 body 서술이 **없다** |
| b | `구현명세서:2764` vs `:2184-2189` | §12-5 는 reject 에 `reason` 만 요구하는데, §8-4 [S-2] `cogito_reject` 는 `action_digest_hex` 와 `nonce` 를 받는다. **§12-5 가 부족하다** |
| c | `구현명세서:2098` | `cogito_acknowledge_indeterminate(a, operator_subject_id, note)` — **어느 잠금을 해제하는지 지정하는 인자가 없다.** `:2261` 은 `indeterminate_locked_count` 가 복수임을 전제한다 |
| d | 명세 전문 | **오류 응답 envelope 정의가 0건.** §12-5·§12-6·§12-7 어디에도 없다. HTTP status 는 `202`(`:2762`)·`429`(`:2737`)·`503`(`:2855`) 세 개만 산발적으로 언급된다 |
| e | `구현명세서:2762` vs `:2763` | 202 응답 모양이 서로 다르다 — `{command_id, state}` vs `{approval_id, state:"approved"}` |
| f | `구현명세서:2763` vs `:2882-2885` | `/api/approve` 가 `state:"approved"` 를 즉시 반환하는데, §12-8 [A-8]은 *"UI 는 승인 클릭을 '성공'으로 표시하지 않는다"* 고 요구한다. **서버가 UI 에게 '성공' 이라는 낱말을 건네고 있다** |
| g | `구현명세서:2783` vs `:1752` | `session.command_id_cache` 를 참조하는데 §7-1 config 의 `session` 블록에는 `max_turns`·`approval_timeout_ms` 뿐이고 그 키가 **없다**. §12-6 `http_server.limits`(`:2813-2814`)에도 없다 |
| h | `구현명세서:2783` | 멱등 캐시의 **키·TTL·퇴거 정책·재시작 정책**이 전부 없다. 용량 256 만 있다 |
| i | `구현명세서:2781` | `GET /api/turns/{turn_id}` 가 *"서버 보관 `TurnOutcome`"* 이라 하는데 **보존 상한이 없다.** 무제한 보관은 장기 세션에서 메모리 누수다 |
| j | `구현명세서:3003` | *"`reason_code` 가 다국어화 이음매다 — 표시 문구가 아니라 코드로 분기한다"* — 그런데 오류 응답에 `reason_code` 를 실으라는 규정이 없다. 실측 M7 이 정확히 그 결과다 |

### 선택지 — 오류 식별자를 §3-4 `reason_code` 에 합칠 것인가

| # | 방식 | 판정 |
| --- | --- | --- |
| 1 | 전송 계층 오류를 §3-4 표(`:275-284`)에 추가 | **기각.** `:271` 이 *"`reason_code` 는 major 버전 내에서 불변"* 이라고 계약했다. CSRF·rate limit 같은 전송 계층 사정으로 그 표가 늘어나면, **코어의 안정 식별자 계약이 웹 계층 변경에 인질**이 된다 |
| 2 | **`error_code` 를 별도 축으로 신설** | **채택.** 두 축은 발생 계층이 다르다. Gate 판정에 도달조차 못한 요청에 Gate `reason_code` 를 붙이는 것은 거짓 정보다 |
| 3 | 자유 문자열만 반환 (현행 M7) | **기각.** §12-12(`:3003`)의 *"표시 문구가 아니라 코드로 분기한다"* 를 구조적으로 불가능하게 만든다 |

### 확정

#### D19-1. POST 7경로 필수 body

**공통 필수 — `command_id`**

```
command_id : UUIDv4 문자열. 36자, 소문자 hex, RFC 4122 표기.
             version nibble == 4 및 variant == 10xx 를 실제로 검사한다.
             위반 -> 400 error_code=command_id_malformed
```

버전·variant 를 실제로 검사하는 이유: 클라이언트가 순차 정수를 UUID 로 위장하면 다른 탭·다른 운영자의 캐시 항목과 충돌한다. 근거 `구현명세서:2783`.

| 경로 | 필수 body | 근거 | 비고 |
| --- | --- | --- | --- |
| `POST /api/turn` | `command_id`, `user_input`(string) | `:2762`, §8-2 `:2079` | `user_input` 바이트 상한 **미결** (아래) |
| `POST /api/approve` | `command_id`, `approval_id`, `action_digest_hex`, `nonce` | `:2763`, §8-4 [S-2] `:2179-2183` | **승인자를 body 에 넣지 않는다** (W5 `:2639`, [S-1] `:2172`) |
| `POST /api/reject` | `command_id`, `approval_id`, `action_digest_hex`, `nonce`, `reason` | `:2764` + [S-2] `:2184-2189` | **모순 (b) 정정** — §12-5 에 digest·nonce 추가 |
| `POST /api/cancel` | `command_id`, `turn_id` | W14 `:2648` | **`turn_id` 를 필수로 신설.** 두 운영자가 동시에 취소를 눌렀을 때 이미 다음 턴이 시작됐으면 엉뚱한 턴을 취소한다. 불일치 → 409 |
| `POST /api/indeterminate/ack` | `command_id`, `note`, `operation_digest_hex` | `:2766` + `G0-RESOLUTION-9.md:148` | **모순 (c) 정정** — 잠금 키가 `operation_digest` 이므로 어떤 잠금을 푸는지 지정해야 한다. **ABI 인자 추가 필요** |
| `POST /api/finalize/retry` | `command_id`, `turn_id` | `:2767` | 어느 턴의 finalize 를 재시도하는지 필요 |
| `POST /api/session/seal` | `command_id`, `reason` | `:2768` | `reason` 은 **미결(제안)**. `:2768` 이 *"되돌릴 수 없음을 UI 가 경고"* 라 요구하는데 서버 측 대응물이 없다 |

- **알 수 없는 최상위 키는 400** (`error_code=body_unknown_field`). 무시하지 않는다. 근거: 불변식 4(fail-closed). 오타 난 `nonce` 가 조용히 무시되면 §8-4 의 nonce 검증이 "필드 없음" 으로 떨어진다.
- **모든 POST body 는 §3-1 CCJ 입력 위생 규칙의 대상**이다 — 중복 키(`input_duplicate_key`), 비-UTF8(`input_not_utf8`), 깊이 초과(`input_depth_exceeded`)는 §3-4(`:275`)의 기존 `reason_code` 를 그대로 쓴다. **여기는 새 코드를 만들지 않는다.**

#### D19-2. 통일 오류 envelope

```json
{
  "error": {
    "error_code": "csrf_token_missing",
    "reason_code": null,
    "message_ko": "요청을 처리할 수 없습니다. 화면을 새로고침한 뒤 다시 시도하십시오.",
    "command_id": "0f2b9c1e-4a7d-4f3b-9c21-8e6a5d4c3b2a",
    "retryable": false,
    "server_time_utc": "2026-08-24T05:12:33.418Z",
    "process_epoch_id": "…"
  }
}
```

| 필드 | 규칙 |
| --- | --- |
| `error_code` | **전송·인증 계층** 안정 식별자. §3-4 `reason_code` 와 **별도 축**. 소문자 snake_case |
| `reason_code` | **코어 Gate 판정이 실제로 일어난 경우에만** 채운다. 그 외 `null`. 값은 §3-4(`:275-284`) 표에서만 고른다 |
| `message_ko` | 표시용 한국어(§12-12 `:3003`). **분기 금지.** 클라이언트는 `error_code`/`reason_code` 로만 분기한다 |
| `command_id` | POST 오류일 때 요청의 값을 그대로 반향. 파싱조차 못 했으면 `null` |
| `retryable` | `effect != none` 경로는 **항상 `false`**. 불변식 9 — 판정 불가는 재시도가 아니라 `indeterminate` 다 |
| `server_time_utc` · `process_epoch_id` | 클라이언트가 재접속·재생 판단에 쓴다 (§12-7 `:2852`) |

**넣지 않는 것** — 예외 메시지 원문, 스택, 파일 경로, 핸들 주소, `payload_json` 원문, 내부 SQL. 근거: §7-5 민감정보(`:1945`), `docs/adr/0004-audit-integrity-and-failure.md:192`.

**M9 정정** — `user_cancelled`·`lockdown_cleared` 는 §3-4 표에 없는 식별자다. 정상 종료는 오류 envelope 을 쓰지 않으므로 `reason_code` 를 실을 자리가 아니다. 두 값을 **삭제**한다.

**HTTP status 사상표** (이 표가 계약이다)

| 상황 | status | `error_code` |
| --- | --- | --- |
| 명령 수리 (모든 POST 정상) | **202** | — |
| 로그아웃 성공 | **204** | — |
| 인증 없음 · 세션 만료 | 401 | `session_required` · `session_expired` |
| 인증됐으나 역할 부족 (§12-5 `:2779` `audit_reader`) | 403 | `role_denied` |
| step-up 미충족·만료 (W11) | **403** | `step_up_required` — **401 이 아니다.** 세션은 유효하다 |
| Origin 부재·불일치 | 403 | `origin_denied` |
| CSRF 토큰 부재·불일치 | 403 | `csrf_token_missing` · `csrf_token_mismatch` |
| content-type 불일치 | 415 | `content_type_unsupported` |
| JSON 파싱 실패 · 필수 필드 누락 · 형식 오류 | 400 | `body_malformed` · `body_missing_field` · `command_id_malformed` |
| body 크기 상한 초과 | 413 | `body_too_large` |
| 알 수 없는 경로 | 404 | `path_not_found` |
| FSM 상태 불일치 (대기 승인 없는데 approve, `turn_id` 불일치) | 409 | `state_conflict` |
| **동일 `command_id` · 다른 body** | **409** | `command_id_conflict` — 캐시 반환이 아니라 충돌이다 |
| 승인 만료 | **409** | `approval_expired` + `reason_code: approval_expired` |
| 명령 큐 포화 (§12-3 `:2737`) | 429 | `queue_saturated` |
| rate limit 초과 (`:2814`) | 429 | `rate_limited` |
| 동시 SSE 스트림 상한 초과 (§12-7 `:2855`) | 503 | `stream_limit_reached` |
| 감사 커밋 실패 | 500 | `audit_commit_failed` + `reason_code: audit_commit_failed`. **write 0회** (불변식 8, ADR-0004 `:166`) |

- **승인 만료를 409 로 정한 이유** — `410 Gone` 은 리소스의 영구 소멸을 뜻한다. 승인은 시간으로 만료되며(§12-12 `:3001`) 같은 action 에 대한 재승인 절차가 §12-8 [A-8](`:2884`)에 정의돼 있으므로 "영구히 사라졌다" 가 아니다. 실측에서 `server.js:376` 이 410 을 쓰고 있어 **정정 대상**이다.

**202 응답 body 통일 — 모순 (e)(f) 정정**

```json
{ "command_id": "…", "state": "accepted", "accepted_at_utc": "…" }
```

| 규칙 | 내용 |
| --- | --- |
| `state` | **명령 수리 상태**이며 판정 결과가 아니다. `accepted`(즉시 제출됨) 또는 `queued`(`command_ack_timeout_ms` 초과, §12-3 `:2738`) **둘 뿐** |
| 경로별 식별자 | 필요하면 추가한다 — approve/reject 는 `approval_id`, turn 은 `turn_id` |
| **`state:"approved"` 를 반환하지 않는다** | 모순 (f) 정정. 서버가 '승인됨' 이라는 낱말을 건네면 §12-8 [A-8](`:2882-2885`)의 *"승인 클릭을 성공으로 표시하지 않는다"* 를 UI 혼자 지켜야 한다. **판정 결과는 SSE 의 verdict 이벤트로만 온다** (W13 `:2647`) |

#### D19-3. 멱등 캐시 — 키 · 용량 · TTL · 퇴거 · 재시작

| 항목 | 확정 | 근거 |
| --- | --- | --- |
| **키** | **`(subject_id, command_id)`** | `command_id` 단독이면 남의 command_id 를 재사용해 **남의 명령 결과를 읽을 수 있다**(승인 결과·턴 상태가 담긴다). 웹 세션 id 는 **넣지 않는다** — 같은 운영자가 세션 만료 후 재로그인해 재시도하는 것은 정상 경로이며, 세션을 키에 넣으면 그 재시도가 **재실행**이 되어 W13 방어가 뚫린다 |
| **`subject_id` 불일치** | **403** (캐시 미스로 처리하지 않는다) | 미스 처리하면 재실행이 일어난다 |
| **저장 값** | `{http_status, response_body, process_epoch_id, body_digest, inserted_at_monotonic_ns}` | `body_digest` 는 409 `command_id_conflict` 판정용 |
| **용량** | **256** (명세값 `:2783`) | 배치는 아래 (g) 정정 |
| **퇴거** | **삽입 순서 FIFO.** LRU 아님 | LRU 는 공격자가 자기 항목을 반복 조회해 남의 항목을 밀어낼 수 있다. FIFO 는 조회로 수명이 늘지 않는다 |
| **TTL** | **미결 — 제안 `900000`ms(15분)** | 하한 논거: `turn_timeout_ms`(§7-1 `:1786` = 180000) + `approval_timeout_ms`(`:1752` = 120000) = 300000 보다 **커야 한다.** 승인 대기 중인 턴의 원래 명령이 캐시에서 빠지면 운영자의 재제출이 재실행이 된다. 여유 3배가 900000. **값은 제품 책임자 결정 필요** |
| **재시작** | **메모리 전용. 영속화 금지** | 재시작하면 코어의 승인·Permit 상태도 사라진다. 캐시만 살아남으면 **존재하지 않는 승인의 성공 응답을 되돌려준다.** 그리고 `process_epoch_id` 가 바뀌면 클라이언트는 §12-7(`:2852`)에 따라 어차피 전체 상태를 다시 조회한다 |
| **epoch 가드** | 조회 시 저장된 `process_epoch_id` ≠ 현재 값이면 **미스** | 메모리 전용이므로 현재는 발생하지 않는다. 장래의 영속화 시도를 구조적으로 막는 불변식으로 둔다 |

**캐시는 1차 방어가 아니다 — 이것을 명문화한다.**

```
1차 방어 : 코어의 단일 사용 Permit (불변식 5)
           소비된 승인의 재제출은 approval_already_consumed 로 거부된다 (§3-4 :281)
2차 방어 : 멱등 캐시  ← 명세 :2783 이 스스로 "두 번째 방어선" 이라 부른다
```

따라서 **캐시 미스(퇴거·TTL 만료·재시작)는 곧 재실행이지만, 그 재실행은 1차 방어가 잡는다.** 캐시가 잡는 것은 "1차 방어가 잡기 전에 도달한 중복" — 즉 이중 클릭이다. 실측 M6 의 무제한 `Map` 은 **1차 방어가 없는 mock 에서만 안전해 보인다.**

**(g) 배치 정정** — `session.command_id_cache` → **`http_server.limits.command_id_cache`**. 근거: 멱등 캐시는 HTTP 재제출 방어이고, **코어는 `command_id` 를 모른다** — §8-2(`:2079-2098`)·§8-4(`:2179-2189`) 어느 시그니처에도 `command_id` 인자가 없다. §7-1 의 `session` 블록(`:1752`)은 코어 설정이다.

#### D19-4. turn outcome 보존 상한 — 모순 (i)

| 항목 | 확정 |
| --- | --- |
| 보존 대상 | `GET /api/turns/{turn_id}`(`:2781`)가 반환하는 `TurnOutcome`(§4-12 `:1202`) |
| 상한 | **미결 — 제안 `turn_outcome_retention = 64`턴, `http_server.limits` 배치** |
| 퇴거 방식 | 삽입 순서 FIFO |
| 퇴거된 turn 조회 | **404 + `error_code: turn_outcome_evicted`**, `message_ko` 에서 `GET /api/audit` 안내 |
| **권위** | **감사 기록이 권위다.** `/api/turns/{id}` 는 편의 캐시이며 유일한 소스가 아니다 |

권위 관계의 근거: §12-7(`:2854`)이 pending 승인에 대해 *"`GET /api/approvals/pending` 이 언제나 권위 있는 소스다. SSE 는 편의일 뿐"* 이라는 동일 구조를 이미 확정했다. 감사 조회는 §12-5(`:2779`) `GET /api/audit`, W17(`:2651`)에 따라 **코어의 `cogito_query_audit` 만** 사용한다.

#### D19-5. GET 쿼리 파라미터 계약

체크리스트 `:91` 이 "POST/GET body schema" 를 요구한다. GET 은 body 가 없으므로 쿼리를 확정한다.

| 경로 | 파라미터 | 규칙 |
| --- | --- | --- |
| `GET /api/state` | 없음 | 파라미터가 있으면 400 |
| `GET /api/approvals/pending` | 없음 | |
| `GET /api/tools` | 없음 | |
| `GET /api/budget` | 없음 | |
| `GET /api/transitions` | 없음 | |
| `GET /api/audit` | `from_seq`(int64) · `limit`(int32) · `session_id`(선택) | §8-5 `cogito_query_audit(a, from_seq, limit, session_id_or_null, out)`(`:2283-2286`)와 **1:1**. `limit` 기본·상한 **200**(§12-5 `:2789`, §8-5 `:2282`). 초과 요청은 절단이 아니라 **400** |
| `GET /api/turns/{turn_id}` | 경로 파라미터만 | `turn_id` 형식 **미결** |
| `GET /api/events` | `from_seq`(선택) | 아래 |

**알 수 없는 쿼리 파라미터는 무시하지 않고 400** (`error_code=query_unknown_param`). 근거: 불변식 4. 오타 난 `limit` 이 조용히 기본값으로 떨어지면 감사 조회 범위가 운영자 의도와 달라지고, 운영자는 **보이지 않는 것을 없다고 판단**한다.

**`GET /api/events` 의 재생 시작점 — 두 입력의 우선순위**

```
① Last-Event-ID 헤더가 있으면 그것이 이긴다        (브라우저 자동 재연결 경로)
② 없고 from_seq 쿼리가 있으면 그것을 쓴다          (최초 연결 경로)
③ 둘 다 없으면 현재 시점부터                        (재생 없음)
④ 값이 현재 process_epoch_id 범위 밖이면 400
```

②를 허용하는 이유: 브라우저 `EventSource` 는 최초 연결에 임의 헤더를 실을 수 없고 `Last-Event-ID` 는 **자동 재연결 시에만** 브라우저가 보낸다고 알려져 있다. 그러나 **1차 자료(WHATWG HTML Living Standard, `EventSource`) 를 대조하지 않았다 — 「미확인」이다.** 확인 결과가 다르면 ②를 삭제하고 헤더 단일 경로로 되돌린다.

### 명세 수정 diff

```diff
 | `POST /api/reject` | `202` | ✅ | `reason` 필수 |
+| `POST /api/reject` | `202` | ✅ | `{approval_id, action_digest_hex, nonce, reason}` 필수 (§8-4 [S-2]) |
-| `POST /api/approve` | `202 {approval_id, state:"approved"}` | ✅ | …
+| `POST /api/approve` | `202 {command_id, state:"accepted", approval_id}` | ✅ | `{approval_id, action_digest_hex, nonce}` 필수. **`state` 는 명령 수리 상태이며 판정 결과가 아니다** (W13 · §12-8 [A-8])
-| `POST /api/cancel` | `202` | — | **명시적 취소만이 취소다** (W14) |
+| `POST /api/cancel` | `202` | — | `{turn_id}` 필수. **명시적 취소만이 취소다** (W14) |
-| `POST /api/indeterminate/ack` | `202` | ✅ | `note` 필수. `line_write_lockdown_` 해제 |
+| `POST /api/indeterminate/ack` | `202` | ✅ | `{note, operation_digest_hex}` 필수. `operation_digest` 로 지정된 잠금만 해제 (G0-05)
-| `POST /api/finalize/retry` | `202` | ✅ | 🟠I 복구 |
+| `POST /api/finalize/retry` | `202` | ✅ | `{turn_id}` 필수. 🟠I 복구 |

-**멱등 제출** — … 호스트는 최근 `session.command_id_cache`(기본 256)개를 보관하고, …
+**멱등 제출** — 모든 POST 는 클라이언트가 만든 `command_id`(UUIDv4, version·variant 검증)를 포함한다.
+호스트는 **`(subject_id, command_id)`** 를 키로 최근 `http_server.limits.command_id_cache`(기본 256)개를
+**삽입 순서 FIFO** 로 보관하며, 중복 제출은 재실행 없이 원래 결과를 반환한다.
+캐시는 **메모리 전용**이고 재시작 시 소멸한다(영속화 금지). 항목은 `process_epoch_id` 와 함께 저장하고
+epoch 불일치는 미스로 처리한다. 동일 키에 **다른 body** 가 오면 캐시 반환이 아니라 `409 command_id_conflict` 다.
+이것은 W13 의 **두 번째** 방어선이다 — 첫 번째는 코어의 단일 사용 Permit(불변식 5)이다.
+`subject_id` 가 다른 요청이 같은 `command_id` 를 제시하면 **403** 이다.
```

```diff
 ### 12-6. 인증·인가·바인드
   "limits": { "max_concurrent_streams": 16, "command_queue_len": 64,
-              "command_ack_timeout_ms": 5000, "rate_limit_per_subject_per_min": 60 }
+              "command_ack_timeout_ms": 5000, "rate_limit_per_subject_per_min": 60,
+              "command_id_cache": 256,
+              "command_id_ttl_ms": null,          /* 미결 — 제안 900000 */
+              "turn_outcome_retention": null }    /* 미결 — 제안 64 */
```

`§12-5` 말미에 **오류 envelope 절**과 **HTTP status 사상표**를 신설한다(위 D19-2 전문).

### 영향

- **Antigravity 인계** — M2(6/7 미수신) · M6(퇴거 없음) · M7(envelope) · M8(응답 모양) · M9(가짜 reason_code) · `server.js:376` 의 410→409 정정.
  특히 `POST /api/session/seal`(`server.js:495`)은 되돌릴 수 없는 조작인데 현재 `command_id` 수신이 **0** 이다.
- **Codex 인계** — `cogito_acknowledge_indeterminate` 에 잠금 식별 인자 추가 (모순 (c)). G0-01 의 MINOR 규칙(`G0-RESOLUTION-9.md:64-65`)상 **기존 함수의 시그니처 변경 = MAJOR** 이므로, **v1.1 을 릴리스하기 전에 반영해야 한다.** 릴리스 후에는 비용이 다르다.
- **테스트** — §12-13 `web/idempotency`(`:3019`)에 다음 3개를 추가한다: ① 다른 `subject_id` 가 같은 `command_id` → 403, ② 같은 키·다른 body → 409, ③ 캐시 퇴거 후 재제출이 **코어의 `approval_already_consumed` 로 거부**되는 2차 방어 확인.
- **G0-23 무관** — 이 절은 CCJ 숫자 표기에 의존하지 않는다.

---

## ③ G0-20 — `assets_digest`

### 모순

| # | 위치 | 문제 |
| --- | --- | --- |
| a | `구현명세서:2263` | `cogito_get_state` 가 `assets_digest` 를 반환한다고 규정. 그런데 `cogito_config_t`(`:2036-2049`)에 이를 넣을 필드가 **없고**, 다른 어떤 ABI 함수도 받지 않는다. **코어가 알 수 없는 값을 반환하도록 규정돼 있다** |
| b | `구현명세서:2960` | *"`assets_digest` 를 … **§13의 config digest 규율**과 동일하게 다룬다"* — **§13 은 「기획안 수정 요청 (🟡Q)」**(`:3036`)이다. 참조 대상이 존재하지 않는다 |
| c | 명세 전문 | `config_digest` 산출 규칙 자체가 **없다.** 등장은 `:2262`(상태 필드)와 `:2476`(골든 입력) 2회뿐 |
| d | `구현명세서:2950` | *"gzip + 바이트 배열 + SHA-256"* — **어느 바이트에 SHA-256 을 거는지 모호**하다. 압축 전인가 후인가 |
| e | `구현명세서:2951` vs `:2960` | 산출물이 `dist.sha256` 인데 상태 필드는 `assets_digest` 다. **같은 값인지 다른 값인지 규정이 없다** |
| f | `작업체크리스트:92` vs `구현명세서:3010-3023` | 체크리스트가 **"tampered asset boot-fail 테스트"** 를 요구하는데 §12-13 테스트 12개에 그런 항목이 **없다.** `web/csp`(`:3022`)는 헤더 스모크일 뿐 |
| g | 실측 M10 | `tools/mock_server/src/server.js:512` 의 값은 **64자 하드코딩 리터럴**이다. 어떤 번들에서도 산출되지 않고 검증되지도 않는다 |

### 선택지 — 집계 방식

| # | 방식 | 판정 |
| --- | --- | --- |
| 1 | gzip 압축 **후** 바이트의 SHA-256 | **기각.** gzip 출력은 라이브러리 버전·압축 레벨에 따라 바이트가 달라져 **재현성이 깨진다.** §10(`:2470-2481`)의 골든 재현성 규율과 같은 이유 |
| 2 | `dist/` 파일들을 이어붙인 단일 스트림의 SHA-256 | **기각.** 파일 경계가 없어 `a.js`+`bc.js` 와 `ab.js`+`c.js` 가 충돌한다 |
| 3 | **파일별 SHA-256 을 정렬해 LP 인코딩으로 집계** | **채택.** 경로·크기·내용이 모두 커버되고 재현 가능하다 |
| 4 | 도메인 태그를 §4-6 목록에 10번째로 추가 | **기각.** G0-26 은 **되돌림 불가**(`G0-RESOLUTION-9.md:27`·`:693`)이고, 태그 9개는 **감사 체인 projection 전용**이다. `assets_digest` 는 감사 링크가 아니라 빌드 산출물 식별자이며 projection 함수 9개 어디에도 속하지 않는다 |

### 확정 — 선택지 3. **되돌림 불가**

#### D20-1. 커버 범위와 집계식

**대상 집합** = `vite build` 산출 `dist/` 트리의 **모든 정규 파일**. 확장자 필터·서브셋 금지.

| 규칙 | 값 | 근거 |
| --- | --- | --- |
| 바이트 | **압축 전 원본 바이트** | 모순 (d) 정정. 선택지 1 참조 |
| 경로 | `dist/` 기준 상대경로. 구분자 **`/` 고정**(Windows `\` 금지). UTF-8 NFC 정규화 | 빌드 호스트 플랫폼에 따라 digest 가 달라지면 W22(`:2656`)의 x86-64 전용 빌드 규율이 무의미해진다 |
| 정렬 | 상대경로의 **바이트 오름차순** | 로케일 의존 정렬 금지 (ADR-0004 D4 가 같은 이유로 로케일 주입을 막는다) |
| 심볼릭 링크 · 빈 디렉터리 | **집합에 포함하지 않는다.** 심볼릭 링크가 발견되면 **빌드 실패** | 링크는 임베드될 수 없다. 조용히 건너뛰면 digest 는 맞고 내용은 다르다 |

```
assets_digest = SHA-256(
      LP("cogito-assets-v1")
   || LP(u64le(file_count))
   || Σᵢ [ LP(relpathᵢ_utf8) || LP(u64le(sizeᵢ)) || LP(sha256_rawᵢ) ]   (정렬 순서)
)

LP / u64le / 원시 32바이트 규칙은 G0-26 확정(G0-RESOLUTION-9.md:459-466)을 그대로 재사용한다.
sha256_rawᵢ 는 원시 32바이트다 (hex 문자열 금지).
표기는 소문자 hex 64자.
```

**`cogito-assets-v1` 은 §4-6 도메인 태그 목록(9개)에 추가하지 않는다.**
- 근거: 위 선택지 4. 태그 문자열이 기존 9개와 겹치지 않으므로 교차 위조 위험도 없다.
- **배치는 웹 호스트 전용**(`tools/web_host/`). 코어는 웹 자산을 모른다 — W2(`:2636`) 단일 스레드 소유 모델, §12-3 신뢰 경계.
- **코어는 이 값을 불투명 64자 hex 문자열로만 취급한다.** 산출은 빌드 시, 검증은 기동 시, 코어는 **보관·재출력만** 한다.
- **미결** — `cogito-assets-v1` 을 §4-6 에 "비-projection 태그" 로 별도 등재할지, 웹 호스트에만 둘지. 제안은 후자.

#### D20-2. 언제 계산되는가 — 2회

| 시점 | 주체 | 산출물 |
| --- | --- | --- |
| **① 빌드** (x86-64 빌드 호스트, W22 `:2656`) | `cmake/EmbedAssets.cmake` (§12-11 `:2975`) | `assets_embedded.cpp` 안의 **컴파일타임 상수** `kAssetsDigestExpected` + 반입용 `dist.sha256` |
| **② 프로세스 기동** | `cogito_web_host` | 임베드된 바이트 배열에서 **다시 계산**한 `assets_digest_actual` |
| ③ 요청 처리 중 | — | **계산하지 않는다.** ②의 값을 재사용한다 |

**②가 이 통제의 전부다.** ①만 있으면 "빌드 시점에 맞았다" 는 주장일 뿐이고, 배포된 바이너리 자체가 변조되면 탐지하지 못한다. ②는 그 바이너리에 **실제로 들어 있는 바이트**를 잰다.

#### D20-3. 불일치 시 fail-closed

```
if (assets_digest_actual != kAssetsDigestExpected) {
    OpsLogger CRITICAL  (양쪽 digest 앞 16자리만 기록)
    → 리스너를 바인드하지 않는다
    → 프로세스 종료, 종료 코드 비0
}
```

| 규칙 | 근거 |
| --- | --- |
| **부분 서빙·읽기 전용 강등 금지** | 승인 UI 가 곧 안전 통제 표면이다. W21(`:2655`) *"설정 가능한 경로는 곧 승인 UI 에 대한 변조 표면"*. 변조된 화면으로 조회만 허용하면 운영자가 **잘못된 화면을 근거로 다음 행동**을 한다 |
| **검사는 바인드 이전** | §12-6 기동검사 1·2(`:2821-2822`)가 이미 바인드 전 실패 규율이다 |
| **감사 DB 에 쓰지 않는다. OpsLogger 에만 남긴다** | 이 시점에 코어 agent 가 아직 생성되지 않았다. "감사에 남겨라" 를 요구하면 순환한다. OpsLogger 는 §7-1(`:1796`) `ops_log` 블록 |
| **앞 16자리만 기록** | §12-8 [A-1](`:2870`)이 승인 화면에도 `action_digest` 앞 16자리만 표시하는 것과 같은 규율 |

§12-6 기동검사(`:2818-2829`) 5종에 **6번째 항목**으로 추가한다.

#### D20-4. 코어 주입 경로 — 모순 (a) 정정

```diff
 typedef struct {
   size_t         struct_size;
   ...
   cogito_mode_t  mode;
+  const char*    assets_digest_hex;   /* NULL 허용 = 웹 미사용.
+                                         비-NULL 이면 소문자 hex 64자 정확히.
+                                         위반 시 cogito_agent_create -> COGITO_ERR_CONFIG */
 } cogito_config_t;
```

| 항목 | 확정 |
| --- | --- |
| 버전 영향 | **없다.** 구조체 **끝** 필드 추가 + `struct_size` 보호이므로 G0-01(`G0-RESOLUTION-9.md:65`)상 MINOR 사유지만, **v1.1 은 아직 릴리스 전**이므로 v1.1 정의에 포함시킨다 |
| NULL 일 때 | `cogito_get_state` 의 `assets_digest` 는 JSON `null` |
| `turn_begin` 기록 | 코어가 보관한 값을 그대로 기록 (§12-11 `:2960`, 체크리스트 `:2599`) |
| **config 파일에 넣지 않는다** | `config/cogito.json`(§7-1)에 기대값을 두면 **변조자가 파일 한 줄만 고쳐 기대값을 맞출 수 있다.** 기대값은 바이너리 안 컴파일타임 상수여야 한다 (W21 `:2655` 의 취지) |

**전달 흐름**

```
빌드 호스트                     Jetson / 폐쇄망
dist/ ──▶ EmbedAssets.cmake ──▶ assets_embedded.cpp
                                  ├─ kAssetsBytes[]
                                  └─ kAssetsDigestExpected   (컴파일타임 상수)
                                        │
                                   기동 시 ② 재계산 대조 (D20-3)
                                        │  일치할 때만
                                        ▼
                        cogito_config_t.assets_digest_hex
                                        ▼
                    cogito_get_state · turn_begin payload
```

#### D20-5. `dist.sha256` 의 지위 — 모순 (e) 정정

- `dist.sha256`(§12-11 `:2951`)의 내용은 **D20-1 의 `assets_digest` 와 동일한 값**으로 통일한다.
- 파일 형식: `<64자 소문자 hex>  dist/` 한 줄 + 개행.
- 근거: 서로 다른 두 개의 "dist 해시" 가 존재하면 **반입 매체 검사와 기동 검사가 서로 다른 것을 보증**하게 되고, 한쪽만 통과하는 상태를 사람이 판별할 수 없다.

#### D20-6. 커버하지 않는 것 (명시)

- **코어 바이너리·설정·정책·도구 레지스트리** — 각각 별도 축이다 (§8-5 `:2262` `config_digest`·`policy_digest`·`registry_digest`).
- **실행 중 메모리 변조** — `assets_digest` 검증은 **기동 시 1회**다. 매 응답마다 재검증하지 않는다.
- **브라우저에 도달한 뒤의 변조** — 프록시·확장·브라우저 캐시는 이 통제의 범위 밖이다. §12-6 의 CSP·HSTS(`:2834-2841`)가 다른 계층에서 다룬다.

> **표기 규율(부록 A `:3060`·`:3066`)** — *"`assets_digest` 가 승인 UI 의 무결성을 보장한다"* 라고 쓰지 않는다.
> 대신 *"빌드 시점 번들과 기동 시점 임베드 바이트의 일치를 확인하고, 불일치 시 기동을 실패시킨다"* 로 쓴다.

### 명세 수정 diff

```diff
 ### 12-11. 빌드 · 폐쇄망 · SBOM
-- **`dist/`는 바이너리에 컴파일해 넣는다**(W21). … `assets_digest`를 `cogito_get_state`와
--  `turn_begin` payload에 기록해 **§13의 config digest 규율**과 동일하게 다룬다.
+- **`dist/`는 바이너리에 컴파일해 넣는다**(W21). 파일시스템 서빙은 승인 UI 에 대한 변조 표면이다.
+
+**`assets_digest` 산출 (되돌림 불가)**
+
+```
+대상 : dist/ 트리의 모든 정규 파일. 압축 전 원본 바이트.
+       경로는 dist/ 기준 상대경로, 구분자 '/' 고정, UTF-8 NFC, 바이트 오름차순 정렬.
+       심볼릭 링크 발견 시 빌드 실패. 빈 디렉터리는 집합에 없다.
+
+assets_digest = SHA-256( LP("cogito-assets-v1") || LP(u64le(file_count))
+                         || Σᵢ [ LP(relpathᵢ) || LP(u64le(sizeᵢ)) || LP(sha256_rawᵢ) ] )
+
+LP·u64le·원시 32바이트 규칙은 §4-6 의 것을 재사용한다.
+"cogito-assets-v1" 은 §4-6 의 도메인 태그 9개에 추가하지 않는다(감사 projection 이 아니다).
+```
+
+- 빌드 시 `assets_embedded.cpp` 에 컴파일타임 상수 `kAssetsDigestExpected` 로 넣는다.
+  `dist.sha256` 은 **같은 값**을 담으며 형식은 `<64hex>  dist/` 한 줄이다.
+- **기동 시 임베드 바이트에서 다시 계산해 대조한다.** 불일치면 OpsLogger CRITICAL(앞 16자리만) 후
+  **리스너를 바인드하지 않고 프로세스를 종료**한다. 부분 서빙·읽기 전용 강등을 금지한다.
+  이 시점에 코어 agent 가 없으므로 감사 DB 가 아니라 OpsLogger 에만 기록한다.
+- 검증에 성공한 값만 `cogito_config_t.assets_digest_hex` 로 코어에 전달되어
+  `cogito_get_state` 와 `turn_begin` payload 에 기록된다.
```

```diff
 ### 12-6. 기동 시 검사 — 실패하면 프로세스 시작 실패
 5. origin_allowlist 가 비었는데 bind_address != 127.0.0.1                    → 시작 실패
+6. 임베드 자산에서 재계산한 assets_digest != kAssetsDigestExpected            → 시작 실패
+   (바인드 이전에 수행한다)
```

```diff
 ### 12-13. 테스트 (`tests/web/`)
+| `web/assets_integrity` | 임베드 바이트 1바이트 변조 시 **기동 실패**(리스너 바인드 0회, 종료 코드 비0) ·
+  정상 번들에서 `cogito_get_state.assets_digest` == `dist.sha256` ·
+  `assets_digest_hex = NULL` 일 때 상태의 해당 필드가 `null` |
```

### 영향

- **Codex 인계** — `cogito_config_t` 필드 추가(D20-4), 기동검사 6번, `cmake/EmbedAssets.cmake` 의 집계식 구현. **v1.1 릴리스 전에 반영해야 버전 비용이 0 이다.**
- **Antigravity 인계** — 실측 M10 의 하드코딩 리터럴(`server.js:512`)은 mock 이므로 그 자체가 결함은 아니지만, **`dist/` 에서 실제로 계산한 값이 아니라는 사실을 코드 주석에 명시**해야 한다. 지금은 실제 구현처럼 보인다.
- **체크리스트 `:92` 의 수용 기준 충족** — "tampered asset boot-fail 테스트" 가 §12-13 에 항목으로 생긴다(모순 (f) 해소).
- **모순 (b)(c) 잔여** — `config_digest` 산출 규칙 자체가 명세에 없다는 문제는 **이 문서가 닫지 않는다.** `assets_digest` 를 그 규칙에 의존시키지 않고 자립적으로 정의함으로써 **회피**했다. `config_digest`·`policy_digest`·`registry_digest` 3개의 산출 규칙은 **G0-26 projection 표(`G0-RESOLUTION-9.md:469~`)의 범위**이며 별도 확인이 필요하다.

---

## 상호 정합성 확인

3건이 서로 모순되지 않는지, 그리고 이미 승인 대기 중인 결정과 충돌하지 않는지 교차 확인했다.

| 쌍 | 확인 |
| --- | --- |
| ① ↔ ② | CSRF 토큰을 `GET /api/state` 로 전달(D18-2) ↔ `GET /api/state` 는 쿼리 파라미터 없음(D19-5). 충돌 없음 |
| ① ↔ ② | 세션 만료 시 401 `session_expired`(D19-2) ↔ 만료가 턴에 영향 없음(D18-3). 오류 응답과 턴 수명이 분리돼 있다 |
| ① ↔ ② | step-up 만료는 403 `step_up_required`(D19-2), 401 이 **아니다**(D18-3). 클라이언트가 재로그인이 아니라 재승격으로 분기한다 |
| ① ↔ ③ | 기동 실패(D20-3)는 리스너 바인드 이전이므로 세션·CSRF 계층이 아직 존재하지 않는다. 순서 충돌 없음 |
| ② ↔ ③ | `GET /api/state` 응답이 `csrf_token`(D18-2)과 `assets_digest`(D20-4)를 함께 싣는다. 둘 다 인증 후에만 도달 가능 |
| ② ↔ G0-05 | `POST /api/indeterminate/ack` 의 `operation_digest_hex`(D19-1)가 `G0-RESOLUTION-9.md:148` 의 잠금 키와 같은 값이다 |
| ③ ↔ G0-26 | `cogito-assets-v1` 을 태그 9개에 **넣지 않음**(D20-1)으로써 **되돌림 불가** 결정을 건드리지 않는다 |
| ③ ↔ G0-01 | 구조체 끝 필드 추가(D20-4)는 `G0-RESOLUTION-9.md:65` 의 MINOR 규칙에 부합하며, v1.1 릴리스 전이므로 버전 변화가 없다 |
| ② ↔ G0-23 | 이 문서는 CCJ 숫자 표기에 의존하지 않는다. G0-23 확정과 독립 |
| ① ↔ W9 | 데모 모드에서 세션·CSRF 를 발급하지 않되 Origin·content-type 은 유지(D18-5) ↔ W9 의 `effect != none` 전면 Deny 는 그대로 |

**모순 없음.** 승인 순서 권고: **③(G0-20) → ②(G0-19) → ①(G0-18)**.
③이 유일한 되돌림 불가이고 빌드 파이프라인이 여기에 묶인다. ①은 G0-17 결정을 기다리는 부분이 남아 있으므로 마지막이다.

---

## G0-17 의존 목록 — ADR `0009-web-trust-boundary` 로 이관

**이 문서는 아래 7건을 추정으로 채우지 않았다.** 전부 현장값이며 제품 책임자 결정이 필요하다
(`docs/g0/G0-RESOLUTION-9.md:697-698`, `docs/STATUS-AUDIT-2026-08-24.md:220`).

| # | 미결 항목 | 이 문서에서 막힌 곳 | 명세 근거 |
| --- | --- | --- | --- |
| 1 | **인증원** — mTLS 클라이언트 인증서 / 배지 리더 / 사내 OIDC | D18-4 `POST /api/auth/login` 의 존재 여부와 프로토콜 | `구현명세서:3027`, `:2802-2803` |
| 2 | **step-up 수단** — badge / pin / mtls 중 실제로 무엇을 쓰는가 | D18-4 `POST /api/auth/step-up` 의 body | `:2807` (후보만 나열) |
| 3 | **`origin_allowlist` 실제 값** | D18-2 (a) Origin 정확 일치의 비교 대상 | `:2808` `["https://line3-hmi.plant.local"]` 은 **예시**다 |
| 4 | **역할 이름 체계** — `operator`·`qa_engineer`·`audit_reader` | D19-2 의 403 `role_denied` 판정 대상 | `:2163`·`:2779` (예시) |
| 5 | **SOD 예외** — `approval.allow_self_approval` 을 열 조건 | D18-3 로그아웃 규약이 교대 인수인계 전제와 결합 | `:2812`, `:2194`, `:3028` |
| 6 | **브라우저 기준선** | D18-1 `SameSite`, D18-2 `Sec-Fetch-Site` 비대칭, D19-5 `EventSource` 제약 | `:2998` |
| 7 | **`session_ttl_ms` 실제값** | D18-1 절대 만료 | `:2805` `3600000` 은 **예시값**이다 |
| 8 | 모순 (d) — *"미인증 요청은 정적 자산 포함 전부 거부"* 의 해석 | D18-4 부트스트랩 | `:3012` |

---

## 미결 목록 — 제품 책임자 결정 필요

G0-17 의존이 **아닌데도** 명세에 값이 없어 이 문서가 확정하지 않은 것들이다. **제안값에는 근거를 붙였고, 임의 기본값을 넣지 않았다.**

| # | 항목 | 제안값 | 근거 | 배치 |
| --- | --- | --- | --- | --- |
| U1 | `command_id_ttl_ms` | **900000** (15분) | `turn_timeout_ms`(180000) + `approval_timeout_ms`(120000) = 300000 보다 커야 한다. 승인 대기 중 원 명령이 빠지면 재제출이 재실행이 된다. 여유 3배 | `http_server.limits` |
| U2 | `turn_outcome_retention` | **64** 턴 | `session.max_turns`(§7-1 `:1752` = 8)의 8배. 감사가 권위이므로 캐시는 작아도 된다 | `http_server.limits` |
| U3 | `POST /api/turn` 의 `user_input` 바이트 상한 | **미정** | 상한의 **존재**는 명세가 전제한다 — §3-4(`:275`)에 `input_too_large` 가 이미 있다. 그러나 §7-1 `limits`(`:1775-1789`)의 `max_action_bytes`(65536)·`max_context_bytes`(1048576)는 **둘 다 user_input 상한이 아니다.** 값을 지어내지 않는다 | `http_server.limits` 또는 §7-1 |
| U4 | `POST /api/session/seal` 의 확인 필드 | `reason`(문자열) | `:2768` 이 *"되돌릴 수 없음을 UI 가 경고"* 를 요구하는데 서버 측 대응물이 없다. reject 의 `reason`(`:2764`)과 대칭 | §12-5 |
| U5 | `turn_id` 형식 | **미정** | §4-12 `TurnOutcome`(`:1202`)에 형식 규정이 없다. `GET /api/turns/{turn_id}` 의 경로 파라미터 검증에 필요 | §4-12 |
| U6 | `cogito-assets-v1` 태그 등재 위치 | **웹 호스트 전용** | 코어는 웹 자산을 모른다(W2·§12-3). §4-6 에 "비-projection 태그" 로 등재할지는 아키텍트 판정 | §4-6 또는 `tools/web_host/` |
| U7 | 전송계층 `error_code` 목록의 명세 편입 위치 | §12-5 말미 신설 절 | §3-4 에 합치면 *"major 내 불변"*(`:271`) 계약이 웹 계층 변경에 인질이 된다 | §12-5 |
| U8 | **`__Host-` 쿠키 접두사** | 붙일 것을 권고 | **미확인.** RFC 6265bis 1차 자료를 대조하지 않았다 | D18-1 |
| U9 | **`EventSource` 의 커스텀 헤더 제약** | `from_seq` 쿼리 허용 | **미확인.** WHATWG HTML Living Standard 1차 자료를 대조하지 않았다. 확인 결과가 다르면 D19-5 ②를 삭제한다 | D19-5 |
| U10 | `config_digest`·`policy_digest`·`registry_digest` 산출 규칙 | — | **명세에 없다**(모순 (c)). `assets_digest` 는 이에 의존하지 않도록 자립 정의했으나, 나머지 3개는 미해결로 남는다. G0-26 projection 표 범위 | §4-6 / G0-26 |

---

## 승인 요청 사항

| 결정 | 승인자 | 이유 |
| --- | --- | --- |
| **③ `assets_digest` 집계식 + fail-closed 규칙 (D20-1 · D20-3)** | 아키텍트 + 안전 책임자 | **되돌림 불가.** 빌드 파이프라인과 `dist.sha256` 형식이 여기 묶인다. 폐쇄망 반입은 물리 매체 재반입을 뜻한다 |
| ③ `cogito_config_t.assets_digest_hex` 추가 (D20-4) | 아키텍트 | **v1.1 릴리스 전에 하면 버전 비용 0.** 이후에는 비용이 다르다 |
| ② `cogito_acknowledge_indeterminate` 인자 추가 (D19-1 모순 (c)) | 아키텍트 | **기존 함수 시그니처 변경 = MAJOR**(`G0-RESOLUTION-9.md:64`). v1.1 릴리스 전에만 무비용 |
| ② 오류 envelope + status 사상표 (D19-2) | 아키텍트 | 되돌림 가능. 단 `error_code` 를 §3-4 에 합치는 선택지는 **되돌리기 어렵다** |
| ② 멱등 캐시 키·퇴거·재시작 (D19-3) | 아키텍트 + 안전 책임자 | W13 재승인 사고의 2차 방어선. 되돌림 가능 |
| ① 세션 쿠키 속성 + CSRF 3중 AND (D18-1 · D18-2) | 아키텍트 | 되돌림 가능 |
| ① 로그아웃 명문 금지 4항 (D18-3) | **안전 책임자** | W14 위반은 *"탭을 닫는 행위가 설비를 `indeterminate` 로 만든다"*(`:2648`)와 같은 등급이다 |
| U1~U10 | 제품 책임자 | 값 결정 |
| **G0-17 의존 8건** | **제품 책임자 (현장값)** | 이 문서가 채우지 않았다 |

**이 보고서로 닫히지 않는 것** — G0-17(별도 질문지), 그리고 U10(`config_digest` 산출 규칙 부재).
`Cogito++_구현명세서.md:2608` 이 규정한 대로 **ADR `0009-web-trust-boundary` 승인 전에는 §12 착수 자체가 금지**되므로,
이 문서의 승인은 §12 착수 허가가 아니라 **ADR 0009 초안의 입력**이다.
