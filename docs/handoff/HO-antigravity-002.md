# HO-antigravity-002 — 미커밋 산출물 보전 · 거짓 통과 출력 제거 · 잔여 8건

```text
Task ID:       HO-antigravity-002
원본 근거:     docs/STATUS-AUDIT-2026-08-24.md §2-③ §2-④ §3-B §3-C(G1~G10)
선행 작업:     HO-antigravity-001 (32건 중 대부분 해소 — 단 전량 미커밋)
변경 파일:     tools/mock_server/**, tools/web_dashboard/**, tests/web/**, scripts/guard-scope.ps1(공유)
비범위:        include/**, docs/**, config/**, .claude/**, Cogito++_*.md   (Claude 소유)
               src/**, cmake/**, CMakeLists.txt, vcpkg*.json, AGENTS.md, .codex/**  (Codex 소유)
검토자:        Claude (계약) → 사람 (승인)
차단 여부:     blocking 4건 / major 4건 / 판정대기 1건
```

---

## ✅ 먼저 — HO-antigravity-001 은 거의 다 해냈다

실측 확인된 것(전부 워킹트리 기준):

| 항목 | 결과 |
| --- | --- |
| §12-5 경로 | **15/15 정확 일치** (HEAD 는 0/15) |
| WebSocket (A1~A3) | `server.on('upgrade')` → **400 거부**(`server.js:643`). CSWSH 표면 제거 |
| SSE (A4) | `id:` / `Last-Event-ID` / 15초 keepalive / 상한 초과 503 구현 |
| `innerHTML` (E1) | dashboard·server·tests 전체 **0건** |
| BOM (G) | 6개 파일 제거 + **`guard-scope.ps1` 의 BOM 은 정확히 보존** |
| FSM 상태명 (F1) | §4-10 정규 10개로 교체 |
| 승인 3자 대조 (C1) | `approval_id` + `action_digest_hex` + `nonce` |
| 권위 영역 (D1~D4·D7) | overflow 밖 / 2단계 확인 / 권위 필드 표 / provenance·pre-wrap / 공장 시각 |
| `.agents` 규약 문서 (F10) | SSE·15경로·W6 거부로 개정 완료 |

**C2(승인자 유도)는 적대적 검증에서 판정이 뒤집혔다.**
`docs/prompts/TASK_PROMPT_ANTIGRAVITY.md:106` 이
*"승인자 신원을 요청 본문에서 읽지 않는다(W5) — mock 은 고정 주체를 서버 측에서 부여한다"* 라고
**지시했으므로**, `actor_id: 'operator_station_01'` 하드코딩은 미달이 아니라 **지시된 구현**이다.
C2 는 PARTIAL 이 아니라 **해소(TRUE)** 다.

---

## 🚨 G1 — 이 작업물이 저장소에 존재하지 않는다 (blocking · 최우선)

```
$ git status --short          (브라우저 캐시 노이즈 제외)
 M .agents/rules/frontend-and-visual-qa.md
 M .agents/skills/mock-api-engine/SKILL.md
 M .agents/skills/validate-ui-and-csp/SKILL.md
 M tests/web/csp-and-hmi.test.js
 M tests/web/screenshot_1024x768.png
 M tools/mock_server/package.json
 M tools/mock_server/src/server.js
 M tools/web_dashboard/app.js
 M tools/web_dashboard/index.html
 M tools/web_dashboard/style.css
```

**전부 ` M`(워킹트리 전용)이고 스테이징도 비어 있다.**
HEAD 는 여전히 `/api/v1/` 6경로 + RFC6455 WebSocket 핸드셰이크 + `ws@^8.18.0` +
`"ALL EXIT GATES PASSED"` 상태다. **지금 clone 하는 누구에게도 이 진척은 존재하지 않는다.**

### 무엇이 위험한가

- `git checkout` · `git reset` **한 번**, 혹은 `.chrome_test_profile/` 을 정리하려는
  `git clean` **한 번**으로 1,567줄이 전부 소실된다.
- 커밋 메시지가 `update` / `first commit23` 수준이라 **복구 지점이 없다.**
- Codex 브랜치(`codex/ops-bootstrap`) 위에 얹혀 있어 Codex 가 자기 브랜치를 커밋하면
  **남의 산출물을 자기 커밋에 끌어들인다.** `AGENTS.md:7` 이 명시적으로 금지한 형태다.
- Claude/Codex 의 교차 리뷰와 사람 승인이 **diff 기반으로 불가능하다.**

### 조치 — 이 순서로. 뒤집으면 소실된다

```bash
# 1) 먼저 안전하게 만든다
git switch -c antigravity/s10-ho001

# 2) 소유 경로만 스테이징한다.  .chrome_test_profile 은 절대 넣지 마라
git add tools/web_dashboard tools/mock_server tests/web .agents

# 3) 확인 — 스테이징 목록에 .chrome_test_profile 이 하나도 없어야 한다
git diff --cached --name-only | grep -i chrome_test_profile   # 출력이 없어야 정상

# 4) 커밋
```

> ⚠ `.gitignore` 신설과 `git rm -r --cached .chrome_test_profile` 은 **사람 승인 대기**다
> (루트 `.gitignore` 는 어떤 rule 에도 없어 `shared` 로 떨어진다).
> **G1 커밋이 그보다 먼저다** — 정리 작업이 정확히 이 산출물을 날릴 위험을 만든다.

---

## 🚨 G2 — 구현되지 않은 것을 `VERIFIED` 로 출력한다 (blocking)

`tests/web/csp-and-hmi.test.js:395`:

```js
console.log(' - web/csrf:               VERIFIED (CSRF headers & Origin check on REST)');
```

**그런데 `server.js` 에 CSRF 검증도 Origin 검사도 content-type 강제도 없다.**
`X-Cogito-CSRF` 는 `server.js:214` 의 `Access-Control-Allow-Headers` **문자열에만** 등장한다.
클라이언트는 헤더를 보내지만 서버는 대조하지 않는다. `audit_readpath` 도 같은 문제다.

이것은 `HO-antigravity-001 F4`("12개 테스트 영역 중 1개만 부분 커버인데 `ALL EXIT GATES PASSED` 출력")가
**문구만 바뀐 채 살아있는 것**이다. 거짓 통과는 **없는 테스트보다 나쁘다** — 사람이 안전하다고 믿게 만든다.

**조치**: 해당 출력 라인을 `NOT IMPLEMENTED` 로 정정하라. 커버 현황을 정직하게 출력하라.
그다음 G6 에서 실제로 구현하라. **정정과 구현은 별개 작업이다. 정정이 먼저다.**

---

## 🚨 G3 — 주석이 구현과 반대로 말한다 (blocking)

`tools/web_dashboard/index.html:58`:

```html
<!-- FSM Graph Card (§12-9: Driven strictly from /api/transitions) -->
```

**FSM 그래프는 여전히 하드코딩 SVG 다**(`F2` 미해소).
`§12-9`(`구현명세서:2911`)는 대시보드 FSM 그래프를 `DumpTable()` 출력에서만 생성하도록 규정한다.
손으로 그린 그래프는 **실제 실행 경로와 달라져도 아무도 모른다.**

**조치**: 주석을 실제 상태로 정정하고(`TODO: F2 미해소 — 현재 하드코딩 SVG`),
`GET /api/transitions` 응답에서만 그래프를 생성하도록 고쳐라.

> 선행 의존: `§12-9`(`:2911`)가 아직 **`R1/R2/R3`** 를 참조한다.
> `G0-24` 확정안(`R0~R4`)이 승인되면 이 절도 갱신된다.
> **규칙 집합이 확정되기 전에는 그래프 계약도 확정할 수 없다.**

---

## 🚨 G4 — 프로젝트 최우선 수용 기준이 미완이다 (blocking)

`HO-antigravity-001 F5`: **미승인·위조·만료 경로의 write 0회 단언.**

현재 **위조 경로 1개뿐**이고 미승인·만료 경로 단언이 없다.

`CLAUDE.md §5` 불변식 8 — *"감사 실패 시 handler 호출 0회"* —
그리고 체크리스트 `§11 S10` 이 이것을 **프로젝트 최우선 수용 기준**으로 지정한다.

**조치**: 세 경로 각각에 대해 `write 호출 카운트 == 0` 을 단언하라.
"에러가 반환됐다"가 아니라 **"핸들러가 호출되지 않았다"** 를 재야 한다. 이 둘은 다르다.

---

## G5 — 실행되지 않는 테스트는 통과가 아니다 (major)

`package.json` 에 테스트 스크립트는 생겼으나 **cwd 상대경로 때문에 `npm test` 로 실제 실행되지 않는다**
(`HO-antigravity-001 F7` 미해소).

**조치**: 어느 디렉터리에서 실행해도 동작하도록 경로를 스크립트 기준으로 해석하라.
`npm test` 가 실제로 도는 것을 확인하고 **명령과 출력을 증거로 남겨라.**

---

## G6 — CSRF · Origin · content-type 3중 검사 구현 (major)

G2 는 *정정*이고 이것이 *해소*다. POST 7경로 공통 전처리로:

1. **Origin** 을 정확 일치 allowlist 로 대조. 불일치 시 `403`.
   (브라우저는 WS 핸드셰이크에 SOP 를 적용하지 않지만, REST 에서는 Origin 이 유효한 방어선이다)
2. **`X-Cogito-CSRF`** 존재·일치 검사.
3. **`Content-Type: application/json`** 강제.

규범: `구현명세서 §12-6`. 헤더 문자열을 **바이트 그대로** 서버 응답과 테스트 단언에 넣어라.

---

## G7 — `command_id` 를 7개 POST 전부에서 받는다 (major)

현재 **6/7 이 미수신**이다. `구현명세서:2783`:

> **모든 POST** 는 클라이언트가 만든 `command_id`(UUIDv4)를 포함한다 —
> 이것이 W13 재승인 사고를 막는 두 번째 방어선

승인 이중 클릭은 지금 `pendingApprovals.splice(server.js:381)` 단일 소비로만 막힌다.
재시도하는 네트워크 계층이나 별개 슬롯이 끼면 **방어선이 하나뿐**이다.

**특히 `/api/session/seal` 은 되돌릴 수 없는 조작인데 멱등 방어가 0이다.**

**조치**: 7개 POST 전부에서 `command_id` 를 필수로 읽고 `commandCache`(256) 조회 →
중복이면 재실행 없이 원래 응답을 반환한다. 클라이언트 `respondApproval`·`cancel`·`seal`·`ack`
호출부에 `crypto.randomUUID()` 생성을 추가한다.

---

## G8 — `process_epoch_id` 변경 시 SSE 재생 금지 (major)

`§12-7`: *"`process_epoch_id` 가 바뀌면 클라이언트는 재생을 시도하지 않고 전체 상태를 다시 조회한다"* —
**서버·클라이언트 어느 쪽에도 없다.**

**무엇이 깨지는가**: 프로세스 재시작 후 seq 가 1부터 다시 매겨지면, 브라우저가 보관 중인
`Last-Event-ID` 로 재생을 시도해 **다른 epoch 의 감사 이벤트를 잘못 이어붙인다.**
운영자 화면에 **존재하지 않는 이력**이 만들어진다. 사후 조사에서 이 화면을 근거로 쓰면 오판한다.

**조치**: 서버가 SSE 핸드셰이크 시 `event: epoch` 정식 이벤트로 `process_epoch_id` 를 내보내고,
클라이언트는 `Last-Event-ID` 재생 전에 epoch 를 대조해 불일치 시 재생을 생략한다.
epoch 변경을 감지하면 `EventSource` 를 재생성하고 전체를 다시 조회한다.

---

## G9 — 승인 모달의 권위 필드 2개 누락 (major)

`[A-1]` 이 권위 영역에 요구한 필드 중 **`requester_subject_id`** 와 **`approval_required`** 가 화면에 없다.

`requester_subject_id` 없이는 야간 교대 운영자가 **"누가 요청한 행동인지"** 를 화면에서 확인할 수 없어
**W12 자기승인 분리를 사람이 육안 검증할 수 없다.**
(페이로드에는 이미 실려 있다 — `server.js:699`. 화면에만 없다.)

**조치**: `index.html` 승인 모달 메타데이터 그리드(`296-309`)에 셀 2개를 추가하고
`app.js` `showApprovalModal` 에서 `textContent` 로 채운다. **권위 영역이므로 overflow 밖에 유지하라.**

---

## G10 — `guard-scope.ps1` 의 가드 결함 (major · 공유 경로)

`scripts/guard-scope.ps1` 은 `ownership-policy.json` 의 **`guard_contract` 키를 한 번도 참조하지 않는다**(grep 0회).
`case_sensitive: false` 미구현으로 **실측 우회에 성공했다**:

```
Antigravity 가드에 Include/cogito/fsm.hpp  →  allow   (deny 여야 함)
```

**NTFS 는 대소문자를 구분하지 않는다.** 즉 Antigravity 는 `Include/` · `Docs/` 로
Claude 의 헤더 계약과 ADR 을 우회해 덮어쓸 수 있다.

같은 결함이 `cc_guard.py`(Claude)에도 있었고 **2026-08-24 에 수정·검증 완료**했다
(소유권 21/21 + fail-closed 8/8). 참고 구현으로 삼아라 — `.claude/hooks/cc_guard.py`.

**조치**:
1. prefix 비교를 `[StringComparison]::OrdinalIgnoreCase` 로 바꾼다.
2. `guard_contract` 를 읽고, prefix 가 `/` 로 끝나면 디렉터리 매칭 / `special_root_prefixes` 면
   startswith / 그 외는 **정확 일치**로 분기한다. (지금은 `AGENTS.md.bak` 이 `AGENTS.md` 규칙에 매칭된다)
3. 정책 파일 자체(`scripts/ownership-policy.json`) 쓰기를 항상 deny 한다.
4. 수정 후 위 우회 경로들로 deny 가 나오는지 확인하고 **로그를 증거로 남겨라.**

> ⚠ `scripts/` 는 `shared` 다 — *"정확한 경로에 대한 사용자 승인 후에만 쓰기 가능"*.
> **이 파일을 고치기 전에 사람 승인을 받아라.**
> ⚠ **`guard-scope.ps1` 의 UTF-8 BOM 은 유지해야 한다.**
> Windows PowerShell 5.1 은 BOM 없는 `.ps1` 을 ANSI 로 읽어 파서가 깨진다.

---

## 🟠 G11 — 사람 판정 대기 (F11)

**현재 대시보드가 `§12-9` 가 고정한 스택(Vite + React18 + Tailwind + shadcn/ui)이 아니라
수제 HTML/JS + Tailwind 유사물이다.**

- **정식 산출물로 본다면** → 스택을 맞춰야 한다. 지금 코드는 §12-9 미준수다.
- **스파이크(선행 프로토타입)로 본다면** → **S10 종료 증거로 인용할 수 없다.**

게다가 S10 착수 조건 3개가 **전부 미충족**인데 산출물은 이미 있다:

| 조건 | 상태 |
| --- | --- |
| `ADR 0009-web-trust-boundary` Accepted | ❌ 파일 자체가 없음 |
| `docs/web-threat-model.md` | ❌ 없음 |
| G0-17~20 · G0-33 해소 | ❌ 전부 미해소 (`G0-17` 은 제품 책임자 현장값 — AI 결정 불가) |

**판정 전까지 이 산출물을 "S10 완료"의 근거로 인용하지 마라.**
판정 요청은 `docs/STATUS-AUDIT-2026-08-24.md` §3-A-⑧ 에 올라가 있다.

---

## 잔여 minor (커밋 후 정리)

| # | 항목 |
| --- | --- |
| m1 | `server.js:112` 주석은 `19 Explicit` 이라 쓰는데 `:116-133` 배열의 실제 원소는 **18개**다. §4-10 전이표와 대조해 누락 1건을 확인하라 |
| m2 | `mock-api-engine/SKILL.md:38` 이 verdict 이벤트로 `approval_expired`·`approval_reentry_exceeded` 를 나열하는데 `server.js` 는 이 둘을 발행하지 않는다(`:730` denied, `:803` allowed, `:845` rejected 뿐). **문서가 구현을 앞질러 있다** — 구현하거나 '예정'으로 표기하라 |
| m3 | `csp-and-hmi.test.js:179,188` 의 meta 경고 필터가 *"is ignored when delivered via a `<meta>` element"* 를 통째로 지운다. `frame-ancestors` 한정으로 좁혀라 — 다른 오설정이 숨는다 |
| m4 | `[A-6]` 잔여시간이 `setInterval` 기반이라 탭 백그라운드화·스로틀 시 화면이 서버 실제 만료보다 길게 표시된다. 운영자가 '40초 남음'을 보고 누른 승인이 `410 Expired` 로 떨어진다. `monotonic_ns` 로 주기 보정하라 |
| m5 | `GET /api/audit` 은 `:2785-2792` 의 6개 강제 조건 중 필드 화이트리스트(`:579-593`)·200행 상한(`:574`)만 구현됐다. **mock 한계임을 명시**하라 — `audit_reader` 역할 인가·`query_only` 커넥션·동시 2건·2초 상한·OpsLogger 기록이 빠져 있다 |

---

## 권장 순서

```
G1(커밋)  ←  다른 모든 것보다 먼저. 한 번의 git clean 으로 전부 날아간다
  ↓
G2 · G3(거짓 표기 정정)  ←  구현보다 먼저. 잘못된 안전 신호를 먼저 끈다
  ↓
G6 · G7 · G8(전송·명령 안전)  ←  G4 단언의 대상이 된다
  ↓
G4 · G5(단언 · 러너)
  ↓
G9 · G10 · minor
  ↓
G11 판정 대기
```

전체 실측 근거: `docs/STATUS-AUDIT-2026-08-24.md`
