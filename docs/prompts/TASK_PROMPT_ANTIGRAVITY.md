# [Antigravity Task] S10 선행: Mock API · 산업용 HMI 승인 UI · G0-33 CSP 실측

> **수신자**: Google Antigravity
> **역할**: 프론트엔드 · 실브라우저 시각/보안 검증
> **소유 영역**: `tools/mock_server/**`, `tools/web_dashboard/**`, `tests/web/**`, `.agents/**`
> **쓰기 금지**: 그 외 전부. 경계는 `scripts/ownership-policy.json` 이 유일한 기준이며 `scripts/guard-scope.ps1` 이 강제한다.

---

> ## ⚠️ 이 문서는 2026-08-21 에 전면 개정됐다
>
> 이전 판이 **명세와 어긋나는 지시**를 담고 있었다. `/api/v1/` 경로, WebSocket, 비규격 FSM 상태명,
> 약화된 CSP, JSON diff 뷰어 — 전부 이 문서가 시킨 것이고 Antigravity 는 충실히 구현했다.
> **드리프트의 원인은 구현이 아니라 이 지시서였다.**
>
> 이전 판으로 만든 `tools/mock_server/`, `tools/web_dashboard/`, `tests/web/` 산출물은
> 인계 티켓 `docs/handoff/HO-antigravity-001.md` 의 항목대로 수정 대상이다.
> 이 문서와 명세가 충돌하면 **명세(`Cogito++_구현명세서.md` §12)가 이긴다.**

---

## 1. 미션

C++ 코어(S7) 완성 전에 **Mock API + 웹 대시보드**를 선행 구축해
**G0-33("browser CSP violation 0")** 과 **§12-8 산업용 HMI 규약**을 실브라우저로 실측·증명한다.

선행 개발의 목적은 **나중에 다시 짜지 않는 것**이다. 그러려면 mock 이 실제 C++ 호스트가
서빙할 계약과 **같은 계약**이어야 한다. 임의 경로·임의 전송방식은 선행 개발의 이점을 없앤다.

---

## 2. Step 1 — Mock API 서버 (`tools/mock_server/`)

### 2-1. 경로는 §12-5 를 그대로 쓴다. `/v1/` 접두사를 붙이지 않는다

**명령 (POST · 전부 `command_id`(UUIDv4) 필수 · 중복 제출은 재실행 없이 원래 결과 반환)**

```
POST /api/turn                 202 {command_id, state}   결과는 이벤트 스트림으로만
POST /api/approve              202 {approval_id, state}  body: approval_id, action_digest_hex, nonce
POST /api/reject               202                       body: approval_id, action_digest_hex, nonce, reason(필수)
POST /api/cancel               202                       명시적 취소만이 취소다 (W14)
POST /api/indeterminate/ack    202                       body: note(필수). line_write_lockdown 해제
POST /api/finalize/retry       202
POST /api/session/seal         202                       되돌릴 수 없음을 UI 가 경고
```

**조회 (GET)**

```
GET /api/state              GET /api/approvals/pending    GET /api/tools
GET /api/budget             GET /api/transitions          GET /api/audit
GET /api/events   (SSE)     GET /api/turns/{turn_id}
```

**`/api/approve` 는 `202` 만 반환한다. 턴 결과를 담지 않는다(W13).**
승인 핸들러가 턴 전체를 기다리면 프록시 타임아웃에 걸리고, 운영자가 재승인을 눌러 이중 재개가 난다.

### 2-2. WebSocket 을 쓰지 않는다 — SSE 단방향만 (W6)

**이것은 취향이 아니라 안전 요구사항이다.**
WebSocket 은 정의상 양방향이라 REST 에만 건 인증·Origin·CSRF·FIFO 를 우회하는
**두 번째 명령 경로**가 된다(불변식 1). 브라우저는 WS 핸드셰이크에 동일출처정책을 적용하지
않으므로 CSWSH 도 막지 못한다.

```
GET /api/events   Content-Type: text/event-stream
  id: <감사 seq>
  event: <kind>
  data: <json>

  - 재연결 시 Last-Event-ID 헤더로 재생한다 (§12-7)
  - process_epoch_id 가 바뀌면 재생하지 않고 전체 상태를 다시 조회한다
  - 15초마다 ": keepalive" 주석 프레임
  - 동시 스트림 상한(기본 16) 초과 시 503
```

**`server.on('upgrade')` 는 명시적으로 거부**하고 소켓을 닫는다 —
`tests/web/single_command_path` 가 단언할 대상이 있어야 한다.
`package.json` 의 `ws` 의존성을 제거한다.

### 2-3. FSM 상태 이름을 만들어내지 않는다

§4-10 이 정의한 10개가 전부다.

```
Idle · Infer · Propose · Gate · AwaitApproval · Execute · Observe · Done · Failed · Cancelled
```

`Thinking` · `GateEvaluating` · `Executing` 같은 이름은 존재하지 않는다.
`GET /api/transitions` 는 `cogito_dump_transitions` 출력 형태(명시 19개 + 보편 규칙 R0~R4)를
그대로 흉내 낸다. **대시보드 그래프는 이 응답에서만 만든다(§12-9).**

### 2-4. 승인 페이로드는 §8-5 필드를 다 담는다

```json
{ "pending_action_id": "...", "pending_approval_id": "...", "tool_name": "...",
  "effect": "write", "risk": "high", "grammar_coverage": "partial",
  "approval_required": true, "canonical_arguments": {}, "before": {},
  "action_digest_hex": "...", "nonce": "...", "policy_rule_id": "...",
  "expires_in_ms": 120000, "requester_subject_id": "..." }
```

`after` 는 **승인 시점에 존재하지 않는다**(실행 전이므로). `tool_result` 이벤트로만 도착한다.
승인 수락 시 `approval_id` + `action_digest_hex` + `nonce` 를 **셋 다** 대조한다.
**승인자 신원을 요청 본문에서 읽지 않는다**(W5) — mock 은 고정 주체를 서버 측에서 부여한다.

### 2-5. 바인드와 인증

`127.0.0.1` 에만 바인드한다. `Access-Control-Allow-Origin: *` 를 쓰지 않는다.
mock 이 인증을 생략하는 것은 허용하되, **`effect != none` 도구는 전부 Deny 로 강제**하고(W9)
화면 상단에 영구 경고 배너를 띄운다. 인증 없는 경로가 설비를 조작할 수 없어야 한다.

---

## 3. Step 2 — 대시보드 (`tools/web_dashboard/`)

### 3-1. §12-8 권위 영역 — 이 절은 안전 요구사항이다

불변식 5 는 승인을 digest 에 결합하지만 **사람은 화면의 문장에 동의한다.** 그 간극이 최대 위협이다.

```
[A-1] 화면 상단 고정. 스크롤·접기 없음. 1024x768 에서 잘리지 않는다.
      overflow-y-auto 컨테이너 안에 넣지 않는다.
      코어 산출 필드만: tool_name · effect · risk · grammar_coverage · approval_required
                        canonical_arguments · before · policy_rule_id
                        action_digest 앞 16자 · 잔여 만료 · requester_subject_id
[A-2] effect != none 이면 위험도를 색상만이 아니라 텍스트로도 쓴다.
[A-3] grammar_coverage != full 이면 "생성 단계 제약 없음 — 런타임 검증만 적용됨" 표시.
[A-4] 모델·RAG·도구 유래 텍스트는 '신뢰할 수 없는 외부 데이터' 라벨이 붙은 별도 영역에
      pre-wrap 평문으로만. 마크다운·HTML·링크·이미지 렌더링 금지. provenance 함께 표시.
[A-5] 승인/거부 버튼 최소 24px 분리, 56x56px 이상, 승인은 2단계 확인.
[A-6] 잔여 시간은 서버가 준 expires_in_ms 와 /api/state 의 monotonic_ns 로 계산한다.
      브라우저 Date.now() 로 계산하지 않는다.
[A-7] before vs requested 는 diff 뷰어가 아니라 권위 필드 '표' 로 표시한다.
      (이전 판이 JSON diff 뷰어를 지시했다 — 그것이 틀렸다)
[A-8] 승인 클릭을 '성공'으로 표시하지 않는다. SSE 의 verdict 이벤트를 받은 뒤에만 최종 상태를
      표시하며 approval_reentry_exceeded / indeterminate_lockdown / approval_expired /
      policy_denied 를 각각 구분해 보여준다.
```

위험도·effect 를 정적 DOM 리터럴로 두지 않는다. 페이로드에서 매번 갱신한다.

### 3-2. XSS 표면을 만들지 않는다

서버 필드로 `innerHTML` 을 조립하지 않는다. 특히 HTML 속성 컨텍스트에 넣지 않는다.
모델 유래 텍스트는 `textContent` 로만 넣는다(§12-10 M-1).

### 3-3. 상시 표기

대시보드는 설비 제어 HMI 가 아니다. 화면에 상시 표기한다:
**"승인 기반 상위 수준 작업 — 안전 계통은 기존 인증 체계가 담당"** (불변식 13)

시각은 공장 현지시각 + 타임존 라벨을 함께 표시한다(§12-12).

---

## 4. Step 3 — CSP 와 검증 (`tests/web/`)

### 4-1. CSP 는 §12-6 을 그대로 쓴다

```
Content-Security-Policy: default-src 'none'; script-src 'self'; style-src 'self';
  img-src 'self' data:; font-src 'self'; connect-src 'self'; worker-src 'self';
  object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none';
  upgrade-insecure-requests
Strict-Transport-Security: max-age=31536000
X-Content-Type-Options: nosniff
Referrer-Policy: no-referrer
Cache-Control: no-store        (모든 /api/* 응답)
```

**`connect-src 'self' ws:` 를 쓰지 않는다.** `ws:` 는 스킴 와일드카드라 임의 호스트로의
WebSocket 을 허용하는 유출 채널이다. SSE 를 쓰면 애초에 필요 없다.

`default-src 'none'` 을 빼지 않는다. 이것이 deny-by-default 의 CSP 표현이다.
클라이언트는 fetch 를 **동일 출처 상대경로**(`/api/turn`)로 호출한다.
`http://localhost:8080/...` 같은 절대 URL 은 `connect-src 'self'` 에 걸린다.

### 4-2. 검증은 헤더 존재가 아니라 실제 위반 0 을 단언한다

```
- SecurityPolicyViolationEvent 리스너를 걸고 count == 0 을 단언한다.
  콘솔 문자열 부분일치로 대체하지 않는다.
- 응답 헤더에 CSP 가 실제로 실려 오는지도 함께 단언한다.
- 1024x768 에서 '권위 영역' 노드에 대해 scrollHeight <= clientHeight 를 단언한다.
  절대 실패할 수 없는 노드를 재지 않는다.
- 승인/거부 버튼 BoundingBox: width·height >= 56, 간격 >= 24
- ★ 승인하지 않은/위조된/만료된 경로에서 write 호출 0회를 단언한다.
  이것이 이 프로젝트 전체에서 가장 중요한 수용 기준이다(§11 S10, 불변식 8).
```

### 4-3. §12-13 이 요구하는 12개 영역

`web/auth` · `web/csrf` · `web/single_command_path` · `web/thread_affinity` ·
`web/approval_ui` · `web/sse_resume` · `web/lifetime` · `web/idempotency` ·
`web/backpressure` · `web/audit_readpath` · `web/csp` · `web/offline`

**`web/auth` · `web/csrf` · `web/audit_readpath` 가 통과하기 전에는 UI 를 확장하지 않는다**(§11 S10).
테스트 러너를 `package.json` 에 실제로 연결한다. 실행되지 않는 테스트는 통과가 아니다.

**"ALL EXIT GATES PASSED" 를 12개 중 1개만 돌린 상태로 출력하지 않는다.**
커버되지 않은 영역을 출력에 명시한다.

---

## 5. 완료 기준

- [ ] 15개 §12-5 경로가 전부 존재하고 `/v1/` 접두사가 없다
- [ ] WebSocket 업그레이드가 거부되고 `GET /api/events` SSE 가 동작한다
- [ ] FSM 상태명이 §4-10 의 10개와 정확히 일치하고 그래프가 `/api/transitions` 에서 생성된다
- [ ] 권위 영역 A-1~A-8 이 1024x768 실측으로 통과한다
- [ ] §12-6 CSP 그대로 적용, `SecurityPolicyViolationEvent` 0건 (G0-33 증거)
- [ ] **미승인 경로 write 0회** 단언이 존재하고 통과한다
- [ ] 12개 테스트 영역 중 커버 현황이 정직하게 보고된다

## 6. 인계 규칙

C ABI·명세·계약 문제를 발견하면 **고치지 말고** `docs/handoff/` 형식으로 Claude 에게 올린다.
`scripts/guard-scope.ps1` 이 경계를 강제하며, 막히는 것은 버그가 아니라 설계대로다.

> **인코딩 주의**: `.ps1` 은 Windows PowerShell 5.1 에서 **UTF-8 BOM 이 필요**하다
> (없으면 ANSI 로 읽혀 파서가 깨진다). 반대로 `.json` 은 BOM 이 있으면 엄격한 파서가 깨진다.
> `.js` / `.html` / `.css` 의 BOM 은 제거한다. 파일 종류별로 다르다.
