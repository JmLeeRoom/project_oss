# Antigravity 작업 규약: Web Frontend & Visual/Security Verification

> **역할**: 프론트엔드 엔지니어링, 실 브라우저 CSP 검증, 1024x768 터치 UI 실측, Mock API 기반 선행 개발.
> **피드백 루프**: 브라우저 렌더링, 콘솔 CSP 에러, DOM BoundingBox 측정, 스크린샷.

---

## 1. 디렉터리 소유권 및 쓰기 금지 경계 (Strict Boundary)

* **소유 및 작업 가능 영역 (Write Allowed)**:
  * `tools/web_dashboard/**` (React, Vite, Tailwind, shadcn/ui, Zustand)
  * `tools/mock_server/**` (독립 선행 개발용 Mock API/WebSocket 서버)
  * `tests/web/**` (Playwright / 브라우저 E2E 테스트)
* **쓰기 절대 금지 영역 (Strict Write Prohibition)**:
  * `include/**` -> **Claude 단독 소유** (헤더 계약)
  * `src/**`, `tests/core/**`, `cmake/**`, `CMakeLists.txt` -> **Codex 단독 소유** (C++ 구현/테스트)
* **경계 위반 금지**:
  * 프론트엔드 작업 중 C ABI나 API 계약의 문제를 발견하면 C++ 코드를 임의로 수정하지 말고, **Claude/Codex에게 이슈 티켓 형태로 요구사항을 인계**한다.

---

## 2. G0-33 & Web 보안 불변식 (Security Invariants)

* **CSP 무결성 (Zero CSP Violations)**:
  * 프로덕션 CSP 헤더:
    `Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self' ws:; font-src 'self'; frame-ancestors 'none'; object-src 'none'; base-uri 'self';`
  * `style-src 'unsafe-inline'` 절대 추가 금지.
  * React Flow나 Recharts 등 차트/그래프 라이브러리가 런타임 인라인 스타일을 주입하여 CSP 에러를 발생시킬 경우, **CSP를 완화하지 않고 순수 정적 SVG(Pure SVG) 컴포넌트로 즉시 대체**한다.
* **외부 CDN 런타임 페치 금지**:
  * Monaco Editor 등 모든 에셋, 폰트, 스크립트는 번들에 포함(`self`)되거나 빌드 시점에 패키징되어야 한다.

---

## 3. §12-8 산업용 HMI UI/UX 실측 기준

1. **권위 영역 (Authority Area)**:
   * 1024x768 뷰포트에서 세로 스크롤 없이(No Scroll) `도구명`, `파라미터 변경(Diff)`, `위험도(Risk)`, `승인/거부 버튼`이 한 화면에 완전히 노출되어야 함.
2. **터치 타깃 및 버튼 간격 (Touch Target & Spacing)**:
   * 승인(Approve) / 거부(Reject) 버튼 간격: **최소 24px 이상 분리**.
   * 모든 액션 버튼 및 터치 가능 타깃: **최소 56px (56x56px)** 크기 확보.
3. **클릭재킹 및 CSRF 방지**:
   * `frame-ancestors 'none'` 검증 및 Double Submit Cookie / CSRF Token 라이프사이클 준수.

---

## 4. 독립 Mock API 선행 개발 원칙

* C++ 코어(S7) 완성을 기다리지 않고, `Cogito++_구현명세서.md` §12-5에 명세된 **15개 API 계약(POST 7개, GET 8개, WebSocket 이벤트)**을 충족하는 Mock 서버를 먼저 구축하여 UI와 G0-33을 즉시 검증한다.
