#!/usr/bin/env python3
"""Cogito++ — SessionStart 컨텍스트 주입.

Claude 의 구조적 약점 중 하나가 '긴 작업에서 앞 결정을 잊는 것'이다(G0-23 사고).
매 세션 시작에 역할·경계·현재 단계를 강제로 다시 읽힌다.

현재 단계는 .claude/state.json 에서 읽는다(없으면 생략). 형식:
  {"stage": "G0", "focus": "G0-23 CCJ 지수 표기", "blocked_by": ["G0-01"], "note": "..."}
"""
import json
import os
import sys

# Windows 기본 stdio 는 cp949 라 U+2014 같은 문자에서 크래시한다.
# 파이프 테스트에서 실제로 UnicodeEncodeError 로 죽었다.
for _s in ("stdin", "stdout", "stderr"):
    try:
        getattr(sys, _s).reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

BASE = """\
[Cogito++ 세션 컨텍스트 — 자동 주입]

■ 이 저장소에서 Claude 의 역할: 계약 관리자 · 감사관
  담당: G0 정정안, ADR 초안, include/cogito/** 헤더 계약, 교차 리뷰, docs/traceability.md
  담당 아님: C++ 구현/테스트(Codex), 프론트엔드/브라우저 검증(Antigravity)

■ 쓰기 가능: include/**, docs/**, config/**, .claude/**, Cogito++_*.md, scripts/(공용)
  쓰기 차단: src/**, tests/**, cmake/**, CMakeLists.txt, tools/(cli|web_host|web_dashboard|mock_server)/**, .agents/**
  → PreToolUse 훅이 실제로 막는다. 막히면 파일을 고치지 말고 소유자에게 이슈로 인계한다.

■ 이 세션에서 지켜야 할 4가지
  1. 사실 주장(포트명·버전·API 시그니처·라이선스)은 1차 자료 확인 후에만 쓴다.
     확인 안 했으면 "미확인"이라고 표기한다. 지어낸 전례: nlohmann-json-schema-validator, encryption-openssl
  2. "확인했다"고 쓰지 않는다. 실행한 명령과 그 출력을 붙인다.
  3. 한 번에 한 단계(G0 항목 하나 또는 S 단계 하나). 끝에 증거를 남긴다.
  4. 작업 시작 전 해당 명세 절을 다시 읽는다. 기억에 의존하지 않는다.

■ 금지 문구는 Cogito++_구현명세서.md 부록 A 를 따른다.
  ("GBNF가 위반을 불가능하게 한다", "Schema가 물리 안전을 보장한다", "WAL이 append-only" 등)
"""


def main() -> None:
    try:
        json.load(sys.stdin)
    except Exception:
        pass

    ctx = BASE
    state_path = os.path.join(ROOT, ".claude", "state.json")
    if os.path.isfile(state_path):
        try:
            with open(state_path, encoding="utf-8-sig") as f:
                st = json.load(f)
            lines = ["", "■ 현재 진행 상태 (.claude/state.json)"]
            if st.get("stage"):
                lines.append(f"  단계: {st['stage']}")
            if st.get("focus"):
                lines.append(f"  초점: {st['focus']}")
            if st.get("blocked_by"):
                lines.append(f"  차단: {', '.join(st['blocked_by'])}")
            if st.get("note"):
                lines.append(f"  메모: {st['note']}")
            ctx += "\n".join(lines) + "\n"
        except Exception as e:
            ctx += f"\n■ .claude/state.json 을 읽지 못했습니다: {e}\n"

    json.dump({
        "hookSpecificOutput": {
            "hookEventName": "SessionStart",
            "additionalContext": ctx,
        }
    }, sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
