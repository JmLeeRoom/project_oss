# Antigravity 작업 규약: Web Frontend & Visual/Security Verification

> **역할**: 프론트엔드 엔지니어링, 실 브라우저 CSP 검증, 1024x768 터치 UI 실측, Mock API 기반 선행 개발.
> **피드백 루프**: 브라우저 렌더링, 콘솔/DOM CSP 에러, DOM BoundingBox 측정, 스크린샷, 미승인 경로 write 0회 단언.

---

## 1. 디렉터리 소유권 및 쓰기 금지 경계 (Strict Boundary)

* **소유 및 작업 가능 영역 (Write Allowed)**:
  * `tools/web_dashboard/**` (대시보드 UI 에셋 및 로직)
  * `tools/mock_server/**` (독립 선행 개발용 Mock API/SSE 서버)
  * `tests/web/**` (실브라우저 E2E/CSP/HMI 테스트)
  * `.agents/**` (Antigravity 소유 제어 평면, 단 `.agents/skills/cogito-stage-owner/` 제외)
* **쓰기 절대 금지 영역 (Strict Write Prohibition)**:
  * `include/**` -> **Claude 단독 소유** (헤더 계약)
  * `src/**`, `tests/**`(web 제외), `cmake/**`, `CMakeLists.txt` -> **Codex 단독 소유** (C++ 구현/테스트)
* **경계 위반 금지**:
  * 프론트엔드 작업 중 C ABI나 API 계약의 문제를 발견하면 C++ 코드를 임의로 수정하지 말고, **Claude/Codex에게 이슈 티켓 형태로 요구사항을 인계**한다.

---

## 2. G0-33 & Web 보안 불변식 (Security Invariants)

* **CSP 무결성 (Zero CSP Violations)**:
  * 프로덕션 규범 CSP 헤더 (§12-6):
    `Content-Security-Policy: default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; font-src 'self'; connect-src 'self'; worker-src 'self'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'; upgrade-insecure-requests`
  * `ws:` 또는 `http:` 스킴 와일드카드 사용 절대 금지 (유출 채널 차단).
  * `style-src 'unsafe-inline'` 절대 추가 금지.
  * React Flow나 Recharts 등 차트/그래프 라이브러리가 런타임 인라인 스타일을 주입하여 CSP 에러를 발생시킬 경우, **CSP를 완화하지 않고 순수 정적 SVG(Pure SVG) 컴포넌트로 즉시 대체**한다.
* **외부 CDN 런타임 페치 금지**:
  * Monaco Editor 등 모든 에셋, 폰트, 스크립트는 번들에 포함(`self`)되거나 빌드 시점에 패키징되어야 한다 (폐쇄망 운영).
* **XSS 방어 (§12-10 M-1)**:
  * 서버 필드로 `innerHTML` 조립 금지. 모델 유래 텍스트는 `textContent`로만 주입.

---

## 3. §12-8 산업용 HMI UI/UX 실측 기준

1. **권위 영역 (Authority Area) [A-1]~[A-8]**:
   * 1024x768 뷰포트에서 세로 스크롤 없이(No Scroll) `도구명`, `파라미터 변경(Before vs Requested 표)`, `위험도(Risk 텍스트 명시)`, `Action Digest 앞 16자`, `잔여 만료 시간`, `승인/거부 버튼`이 완전히 노출되어야 함.
   * `before` vs `requested`는 JSON diff가 아니라 **권위 필드 표(Table)**로 렌더링 ([A-7]).
   * 모델·RAG 유래 텍스트는 '신뢰할 수 없는 외부 데이터' 라벨이 붙은 별도 영역에 **pre-wrap 평문**으로만 표시 ([A-4]).
2. **터치 타깃 및 버튼 간격 (Touch Target & Spacing) [A-5]**:
   * 승인(Approve) / 거부(Reject) 버튼 간격: **최소 24px 이상 분리**.
   * 모든 액션 버튼 및 터치 가능 타깃: **최소 56px (56x56px)** 크기 확보.
   * 승인 버튼은 오조작 방지를 위한 **2단계 확인(2-Step Confirmation)** 적용.
3. **시간 표시 및 상시 표기 (§12-12 / 불변식 13)**:
   * 공장 현지시각 + 타임존 라벨 상시 표시 (`YYYY-MM-DD HH:mm:ss KST (UTC+09:00)`).
   * 화면 상단 상시 표기: **"승인 기반 상위 수준 작업 — 안전 계통은 기존 인증 체계가 담당"** (불변식 13).

---

## 4. 독립 Mock API 선행 개발 원칙

* C++ 코어(S7) 완성을 기다리지 않고, `Cogito++_구현명세서.md` §12-5에 명세된 **15개 API 계약(명령 POST 7개, 조회 GET 8개, SSE 단방향 스트림)**을 충족하는 Mock 서버를 구축하여 UI와 G0-33을 즉시 검증한다.
* **WebSocket은 명시적으로 거부(400 Bad Request)하고, 이벤트 전송은 `GET /api/events` SSE만 사용한다 (W6)**.
* **최우선 검증 기준**: 미승인·위조·만료·거부 경로에서 **write 호출 0회**를 단언한다 (§11 S10, 불변식 8).

