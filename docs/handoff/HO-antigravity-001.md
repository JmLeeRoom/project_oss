# HO-antigravity-001 — S10 선행 산출물의 §12 계약 이탈 32건

```text
Task ID:       HO-antigravity-001
원본 근거:     Cogito++_구현명세서.md §12 (12-1 W6/W13/W14/W16, 12-5, 12-6, 12-8, 12-9, 12-10, 12-13), §4-10, §8-5
선행 작업:     docs/prompts/TASK_PROMPT_ANTIGRAVITY.md 개정 (2026-08-21 완료)
변경 파일:     tools/mock_server/**, tools/web_dashboard/**, tests/web/**, .agents/rules/**, .agents/skills/**
구현 범위:     아래 A~F 그룹
비범위:        include/**, docs/**, config/**, src/** (Claude/Codex 소유)
검토자:        Claude (계약) → 사람 (승인)
차단 여부:     blocking 17건 / major 15건
```

## ⚠️ 먼저 읽을 것 — 이건 구현 잘못이 아니다

이 32건의 대부분은 **`docs/prompts/TASK_PROMPT_ANTIGRAVITY.md` 이전 판이 시킨 대로 만든 결과**다.
그 문서는 Claude 소유이고, `/api/v1/` 경로·WebSocket·비규격 FSM 상태명·약화된 CSP·JSON diff 뷰어를
명시적으로 지시하면서 스스로를 "§12-5 준수"라고 잘못 표기했다. **원인은 지시서였다.**

지시서는 개정됐다. 이 티켓은 그 개정에 맞춰 산출물을 정렬하는 작업이다.

---

## A. 전송 계층 — blocking

| # | 항목 | 현재 | 있어야 할 것 |
| --- | --- | --- | --- |
| A1 | **WebSocket 이 유일한 이벤트 전송** | `server.js:387` `server.on('upgrade')` → 101 Switching Protocols. `:422` 인바운드 `trigger_turn` 명령 수신. `app.js:15` `new WebSocket(...)` | **W6 위반.** `upgrade` 를 명시 거부(400 + destroy)하고 `GET /api/events` SSE 로 교체 |
| A2 | 인바운드 WS 명령 | `trigger_turn` 이 인증·Origin·CSRF·`command_id`·FIFO 를 전부 우회 | 불변식 1(단일 실행 경로) 위반. 명령은 REST 만 |
| A3 | Origin 미검사 | `req.headers.origin` 참조 0회 | 브라우저는 WS 핸드셰이크에 SOP 를 적용하지 않는다(CSWSH) |
| A4 | SSE 부재 | `text/event-stream` · `EventSource` · `Last-Event-ID` 전부 0 hit | `id:`=감사 seq, `Last-Event-ID` 재생, `process_epoch_id` 변경 시 재생 금지, 15초 keepalive, 상한 초과 503 |
| A5 | 미사용 의존성 | `package.json` 이 `ws@^8.18.0` 선언, `server.js` 는 import 안 함 | 제거 |

## B. API 계약 — blocking

| # | 항목 | 현재 | 있어야 할 것 |
| --- | --- | --- | --- |
| B1 | **§12-5 경로 15개 중 0개 일치** | `/api/v1/session/start`, `/api/v1/agent/turn`, `/api/v1/approval/respond`, `/api/v1/audit/logs`, `/api/v1/tools/registry`, `/api/v1/fsm/state` 6개뿐 | `/v1/` 제거 + §12-5 이름으로 교체 |
| B2 | approve/reject 통합 | `/approval/respond` 가 문자열로 분기 (`:158`) | §12-5 는 서로 다른 필수 body 를 갖는 **별개 엔드포인트**다 |
| B3 | 절대 URL 호출 | `app.js:137,157,226,235,240` `fetch('http://localhost:8080/...')` | 동일 출처 상대경로. 절대 URL 은 `connect-src 'self'` 에 걸린다 |
| B4 | **W16 복구 경로 4개 전부 부재** | `cancel`·`seal`·`finalize`·`indeterminate` 문자열 0 hit | 없으면 §6-3(🟠I)·§6-4(🟠J) 탈출구가 웹 배포에 존재하지 않는다 |
| B5 | 조회 경로 부재 | `/api/approvals/pending`·`/api/budget`·`/api/turns/{id}`·`/api/transitions` 없음 | `pending` 은 §12-7 상 **권위 소스**다. 교대 인수인계가 여기 걸린다 |
| B6 | 단일 슬롯 pending | `let currentPendingApproval = null` | `kMaxPending=64` 모델을 표현할 수 없다 |
| B7 | `command_id` 부재 | 어떤 POST 도 안 받음 | W13 2차 방어선. 승인 이중 클릭이 이중 재개가 된다 |
| B8 | 202 의미론 위반 | 승인이 곧바로 실행으로 이어짐 | `202` 만 반환, 결과는 SSE (W13) |

## C. 승인 안전 — blocking

| # | 항목 | 현재 | 있어야 할 것 |
| --- | --- | --- | --- |
| C1 | **digest·nonce 미검증** | `approval_id` 만으로 승인 성립 | §8-4 [S-2]: `approval_id`+`action_digest_hex`+`nonce` **셋 다** 대조 |
| C2 | **승인자를 요청 본문에서 읽음** | 클라이언트 제공 문자열 | W5·[S-1] 명시 금지. 서버가 인증 세션에서만 유도 |
| C3 | W12 자기승인 분리 불가 | `requester_subject_id` 가 없음 | 페이로드에 포함해야 분리 검사가 가능 |
| C4 | PENDING_APPROVAL 필드 7개 누락 | 11개 중 5개만 표시 | §8-5 전체. `grammar_coverage` 없으면 [A-3] 경고가 영원히 안 뜬다 |
| C5 | risk/effect 가 정적 DOM 리터럴 | `showApprovalModal` 이 갱신 안 함 | 페이로드에서 매번 갱신 |
| C6 | [A-8] 위반 | 승인 클릭 즉시 성공 표시, 모달 먼저 닫힘 | verdict 이벤트 수신 후에만 최종 상태. 4가지 결과를 구분 표시 |

## D. 권위 영역 (§12-8) — blocking + major

| # | 항목 | 조치 |
| --- | --- | --- |
| D1 | 권위 영역이 `overflow-y-auto` 안에 있고, no-scroll 단언이 **절대 실패할 수 없는 노드**를 잰다 | 컨테이너 밖으로 빼고 권위 영역 노드 자체를 잰다 |
| D2 | 승인이 단일 클릭, 2단계 확인 없음 | [A-5] |
| D3 | before/requested 를 JSON diff 뷰어로 렌더 + `JSON.stringify` 재직렬화 | [A-7] 권위 필드 **표**. CCJ 바이트를 그대로 표시 |
| D4 | untrusted 영역 없음, provenance 없음, pre-wrap 아님 | [A-4] |
| D5 | 잔여 시간 미표시, `expiresInSeconds` (단위 불일치) | [A-6] `expires_in_ms` + `/api/state` 의 `monotonic_ns` |
| D6 | 상시 범위 배너 없음, insecure 모드 배너/W9 Deny 없음 | 불변식 13 |
| D7 | 시각 표시 전무 | 공장 현지시각 + 타임존 라벨 (§12-12) |

## E. XSS·CSP — blocking + major

| # | 항목 | 조치 |
| --- | --- | --- |
| E1 | **2개 렌더 경로가 서버 필드로 `innerHTML` 조립** (HTML 속성 컨텍스트 포함) | `textContent` 로 교체 (§12-10 M-1) |
| E2 | CSP 가 mock·meta·rules 3곳에서 각각 다름. `connect-src 'self' ws:` 스킴 와일드카드 | §12-6 헤더 그대로. `default-src 'none'` 포함, `ws:` 제거 |
| E3 | `object-src`·`base-uri`·`form-action`·`worker-src` 누락, HSTS·nosniff·no-store 누락 | 추가 |
| E4 | `Access-Control-Allow-Origin: *`, Origin·content-type·CSRF 미검사 | §12-6 |
| E5 | 모든 인터페이스에 바인드, TLS·인증·기동검사 없음 | `127.0.0.1` 고정 (W8) |

## F. FSM · 테스트 · 감사뷰 — blocking + major

| # | 항목 | 조치 |
| --- | --- | --- |
| F1 | **§4-10 에 없는 상태명 5개 사용, 있는 것 6개 누락** | `Idle Infer Propose Gate AwaitApproval Execute Observe Done Failed Cancelled` |
| F2 | `GET /api/transitions` 없음 + FSM 그래프가 손으로 그린 SVG | §12-9 가 금지한 바로 그것. `/api/transitions` 응답에서만 생성 |
| F3 | `session/start` 가 전이 함수를 거치지 않고 `state=Idle` 직접 대입 | Dispatch 경유 |
| F4 | **12개 테스트 영역 중 1개만 부분 커버인데 "ALL EXIT GATES PASSED" 출력** | 커버 현황을 정직하게 출력 |
| F5 | **미승인·위조·만료 경로의 write 0회 단언이 어디에도 없음** | 프로젝트 최우선 수용 기준 (§11 S10, 불변식 8) |
| F6 | CSP 검사가 콘솔 문자열 부분일치, 응답 헤더 미검사 | `SecurityPolicyViolationEvent` count==0 + 헤더 단언 |
| F7 | 테스트가 러너에 연결 안 됨 (`package.json` 스크립트 없음, cwd 상대경로) | 실행되지 않는 테스트는 통과가 아니다 |
| F8 | G0-33 을 "비준된 결정 / 측정 완료"로 인용 | **G0-33 은 미해소 차단 항목**이다. 측정도 프로덕션 번들·규범 CSP 로 하지 않았다 |
| F9 | 감사 뷰가 임의 컬럼명, 체인·귀속 필드 전무, 검증 뱃지 하드코딩 | §7-4 컬럼. 역할·limit·from_seq 강제 |
| F10 | `.agents/rules/frontend-and-visual-qa.md` 와 `mock-api-engine` 스킬이 WebSocket·잘못된 경로를 "§12-5" 로 명시 | 표준 규약 문서를 개정된 지시서에 맞춰 수정 |

## G. 인코딩

`tools/web_dashboard/{index.html,app.js,style.css}`, `tools/mock_server/{src/server.js,package.json}`,
`tests/web/csp-and-hmi.test.js` 에 **UTF-8 BOM** 이 있다. `index.html` 의 BOM 은 `<!DOCTYPE>` 앞에 온다.

> ⚠️ **`.ps1` 은 반대다.** Windows PowerShell 5.1 은 BOM 이 없으면 `.ps1` 을 ANSI 로 읽어 파서가 깨진다.
> `scripts/guard-scope.ps1` 은 BOM 을 **유지**해야 한다. 일괄 제거하지 말 것.

## 권장 순서

```
1) A(전송) + B(경로)   ← 이 둘이 나머지의 전제다. 먼저 고치지 않으면 C~F 를 두 번 고친다
2) E(CSP·XSS)          ← G0-33 증거의 전제
3) C + D(승인 안전)
4) F(FSM·테스트)
```

## 확인 필요 (needs_authority_ruling)

- **F11**: 대시보드가 §12-9 가 고정한 스택(Vite+React18+Tailwind+shadcn/ui)이 아니라
  수제 HTML/JS + Tailwind 유사물이다. **스파이크로 인정하고 폐기할 것인지, 정식 산출물로 볼 것인지**
  사람 판정이 필요하다. 정식이면 스택을 맞춰야 하고, 스파이크면 S10 종료 증거로 인용할 수 없다.
