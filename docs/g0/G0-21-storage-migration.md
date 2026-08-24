# G0-21 정정 보고서 — 감사 저장소 마이그레이션 · 용량 운영 · 보존/백업/복구

| | |
| --- | --- |
| **대상** | G0-21 (`Cogito++_개발_작업체크리스트.md:93` — "SQLite schema migration/versioning, WAL/디스크 임계값, 보존·백업·복구 정책이 없다") |
| **상태** | **Proposed — 사람 승인 대기** |
| **작성** | Claude (계약 관리자 · 감사관) |
| **기준일** | 2026-08-24 |
| **원본** | `Cogito++_구현명세서.md` §7-4 · §7-5 · §7-1 · §6-3 · §6-4 · §12-5, `Cogito++_개발_작업체크리스트.md` S5-06~S5-08 · S11-08 · S11-09, `Cogito++_구현_요구사항.md` §목록 |
| **관련 ADR** | `0004-audit-integrity-and-failure`(Proposed) — 이 문서의 **전제**이지 대체가 아니다 |
| **관련 헤더** | `include/cogito/ops_log.hpp`, `include/cogito/ids.hpp` |
| **되돌림** | **항목별로 다르다.** §1(스키마 버전)·§4(append-only)는 되돌림 불가에 가깝고, §2(임계값)·§3(보존값)은 운영 파라미터라 조정 가능 |

> **읽는 법** — 각 항목은 `[모순] → [선택지] → [확정] → [명세 수정 diff] → [영향]` 순이다
> (`docs/g0/G0-RESOLUTION-9.md:12` 의 규약을 따른다).

---

## 0. 선반영 고지 — 반드시 먼저 읽을 것

이 문서는 **아직 승인되지 않은(Proposed) 결정 두 건을 전제로 선반영**한다.

| 선반영한 것 | 출처 | 이 문서가 의존하는 지점 |
| --- | --- | --- |
| ADR-0004 **D9** (해시체인 규범: `prev_hash` 최초값 32바이트 zero, `seq` 는 해시 입력 아님, append-only 는 trigger+authorizer 가 강제) | `docs/adr/0004-audit-integrity-and-failure.md:158`~`:168` | §1-확정(마이그레이션 additive-only 근거), §3-확정(체인 승계), §4 전체 |
| ADR-0004 **D7** (`operation_digest` 신설, `indeterminate` 잠금 키) | `docs/adr/0004-audit-integrity-and-failure.md:136`~`:145` | §3-확정(복구 후 lockdown 재수립) |

`docs/adr/0004-audit-integrity-and-failure.md:3` 은 `**상태**: Proposed` 다.
**ADR-0004 가 거부되거나 D7/D9 가 바뀌면 이 문서의 §1·§3·§4 를 다시 써야 한다.**
현재 최고 권위는 `Cogito++_구현명세서.md` 이며(`CLAUDE.md:60`), 이 문서는 명세를 고치지 않고 **수정 제안**만 담는다.

또한 이 문서는 **파일 하나만 쓴다**는 제약 아래 작성되었다.
따라서 아래 §A 에서 지적하는 `.claude/skills/adr-draft/SKILL.md` 의 매핑 오류도 **고치지 않고 제안만** 한다.

---

## A. 먼저 — 조용한 커버리지 공백 (G0-21 은 실제로 아무 ADR 도 다루지 않는다)

### 모순

`.claude/skills/adr-draft/SKILL.md:16` 의 필수 ADR 표는 다음과 같다.

```
| 0004 | 감사 무결성과 실패 정책 — 해시체인, finalize/seal | G0-06, G0-21 |
```

즉 **G0-21 은 ADR-0004 가 닫는 것으로 표기**되어 있다. 그런데 실제로 작성된 ADR-0004 는 그렇지 않다.

| 확인 지점 | 실제 내용 |
| --- | --- |
| `docs/adr/0004-audit-integrity-and-failure.md:6` | `**관련 G0**: G0-23(CCJ 지수), G0-05(operation digest), G0-26(도메인 태그·projection)` — **G0-21 도 G0-06 도 목록에 없다** |
| 같은 파일 D1~D8 (`:46`~`:156`) | CCJ v1 숫자 규칙 · LP 인코딩 · 도메인 태그 9개 · projection. 저장소 운영과 무관 |
| 같은 파일 D9 (`:158`~`:168`) | 해시체인·실패 정책 재확인 6줄. **schema 버전·WAL·디스크·보존·백업·복구는 한 줄도 없다** |
| 같은 파일 "검증 방법" (`:218`~`:232`) | 마이그레이션 테스트 없음. `audit/chain.*`, `audit/epoch` 뿐 |

**표만 보면 커버된 것처럼 보이지만 실제로는 공백이다.** 그리고 같은 행의 **G0-06**
(`Cogito++_개발_작업체크리스트.md:78` — "`RetryFinalize()` 가 원래 `TurnOutcome` 대신 새 Failed outcome 으로 `turn_end` 를 만들 수 있다")
도 마찬가지다. ADR-0004 D9 는 `RetryFinalize()`/`SealSession()` 의 **존재**만 재확인하고,
`Cogito++_구현명세서.md:1658`~`:1663` 의 `RetryFinalize()` 가 `tmp.status = TurnStatus::Failed` 로
**새 payload 를 만드는 결함 자체는 다루지 않는다.**

> 즉 SKILL.md:16 의 한 행이 **두 개의 G0 를 동시에 "닫힌 것처럼" 보이게** 하고 있다.
> 이 문서는 그중 G0-21 만 다룬다. **G0-06 은 여전히 미해소이며 별도 담당자가 필요하다.**

### 번호 예약 상황 (실측)

```
docs/adr/ 실제 파일 : 0001-fsm-turn-and-action.md, 0004-audit-integrity-and-failure.md  (2개뿐)
SKILL.md:11~:21 예약 : 0001 FSM / 0002 빌드·폐쇄망(G0-11,12) / 0003 승인·신원(G0-02,03)
                       0004 감사 무결성 / 0005 timeout·멱등(G0-05,28) / 0006 Schema dialect(G0-10,27)
                       0007 외부 데이터 경계 / 0008 패키징(G0-22) / 0009 Web(G0-17~20,33)
```

`Cogito++_개발_작업체크리스트.md:170` 은 "최소 ADR 0001~0008" 을, `:261` 은 "ADR 0001~0008 이 승인 상태다" 를 요구한다.
**0001~0009 는 전부 다른 주제로 예약되어 있고, 비어 있는 최소 번호는 `0010` 이다.**

### 선택지

| # | 안 | 장점 | 단점 |
| --- | --- | --- | --- |
| **(a)** | **ADR-0004 를 확장**해 D10~D13 으로 저장소 계약을 흡수 | 해시체인과 저장소 계약이 한 문서. SKILL.md 표를 고칠 필요 없음 | ADR-0004 는 `:9`~`:10` 에서 **"되돌림 불가. 먼저 승인하라"**, `:209` 에서 **"S1 착수 전에 승인해야 한다"** 고 선언한 문서다. 반면 G0-21 은 **미결 운영값이 9건**이고 착수 단계가 S5/S11 이다(체크리스트 `:669`, `:1713`). 되돌림 불가 결정의 승인이 **아직 정해지지 않은 현장값에 인질로 잡힌다** |
| **(b)** | **`ADR-0010-audit-storage-operations` 신설.** ADR-0004 는 손대지 않고 D9 를 참조만 한다 | 승인 시점 분리(ADR-0004=S1 전, ADR-0010=S5 전). 되돌림 불가/가능이 문서 단위로 깨끗하게 갈림. 미결 9건이 ADR-0004 승인을 막지 않음 | SKILL.md:11~:21 의 "필수 ADR 9개" 표를 넘어선다 → 표에 행 추가 + `:16` 에서 G0-21 제거 필요. 체크리스트 `:170`·`:261` 의 "0001~0008" 문구와도 어긋난다 |
| (c) | G0-21 을 ADR-0005(timeout·재시도·멱등성)로 이관 | 새 번호 불필요 | **부적합.** ADR-0005 의 주제는 `indeterminate`·`operation_digest`(SKILL.md:17)이며 저장소 용량·보존과 접점이 없다. 주제 응집이 무너진다 |
| (d) | ADR 없이 이 G0 문서 + 운영 Runbook(체크리스트 S11-09)만으로 종결 | 문서 수 최소 | `Cogito++_개발_작업체크리스트.md:1721` 이 **"retention/purge 는 append-only 계약과 충돌하므로 archive/anchor/새 chain 시작 절차를 ADR로 정한다"** 고 명시적으로 ADR 을 요구한다. 규범 위반 |

### 권고 (판정은 사람이 한다)

**(b) 를 권고한다.** 근거는 세 가지다.

1. `Cogito++_개발_작업체크리스트.md:1721` 이 이 주제에 대해 **ADR 을 명시적으로 요구**한다 → (d) 탈락.
2. ADR-0004 는 자기 문서 안에서 "**S1 착수 전 승인**"(`:209`)을 요구하는데, G0-21 의 결정 대부분은
   **제품 책임자의 현장값 승인 없이는 확정될 수 없다**(보존 기간·디스크 임계값·백업 보관처).
   두 승인을 한 문서에 묶으면 **결정론 기반(S1)이 공장 운영값 결정을 기다리게 된다.** 이것이 (a) 를 기각하는 결정적 이유다.
3. ADR-0004 가 **G0-06 도 실제로는 닫지 못하고 있다**(위 §A-모순). 0004 를 더 부풀리는 것보다,
   0004 의 실제 범위를 `:6` 의 자기 선언(G0-23·G0-05·G0-26)에 맞게 **줄이는 방향**이 정합적이다.

> **단, (b) 를 택하더라도 ADR-0010 은 ADR-0004 D9 를 재정의하지 않고 참조만 한다.**
> 해시체인 규범이 두 문서에 중복 정의되면 `docs/adr/0004-audit-integrity-and-failure.md:13`~`:14` 가
> 경계한 바로 그 중복이 다시 생긴다.

### 제안하는 SKILL.md 수정 (이 문서는 실행하지 않는다)

```diff
-| 0004 | 감사 무결성과 실패 정책 — 해시체인, finalize/seal | G0-06, G0-21 |
+| 0004 | 감사 무결성과 실패 정책 — CCJ v1, LP digest, 해시체인 | G0-23, G0-05, G0-26 |
+| 0010 | 감사 저장소 운영 — schema migration, WAL·디스크 임계값, 보존·백업·복구 | G0-21 |
```

**G0-06 은 이 diff 로 갈 곳을 잃는다.** 0004 로 되돌릴지 0001(FSM·finalize)로 옮길지는
**이 문서의 범위 밖이며 별도 판정이 필요하다.** 지금 상태로 두면 매핑표가 계속 거짓을 말한다.

---

## 1. Schema 버전 관리

### 모순

| 위치 | 서술 |
| --- | --- |
| `Cogito++_개발_작업체크리스트.md:669` | "원본 §7-4 DDL을 migration 001로 만든다" |
| `Cogito++_개발_작업체크리스트.md:1715` | "schema version table과 forward-only migration을 구현한다" |
| `Cogito++_개발_작업체크리스트.md:676` | "migration 중/실패/재실행/낮은 schema version/높은 schema version을 테스트한다" |
| `Cogito++_구현명세서.md:1887`~`:1934` (§7-4 DDL 전문) | **`schema_migration` 테이블도 `PRAGMA user_version` 도 없다.** 마이그레이션 번호 개념 자체가 없다 |

즉 **테스트할 대상이 정의되어 있지 않다.** 구현자는 "migration 001" 이 무엇인지 알 수 없다.

**게다가 이름이 충돌한다.** `Cogito++_구현명세서.md:1907` 의

```sql
  schema_version   INTEGER NOT NULL,
```

는 **DB 스키마 버전이 아니다.** 이 값은 `Cogito++_구현명세서.md:1442` 에서

```cpp
      .Str(canonical_payload).U64(static_cast<std::uint64_t>(e.schema_version))
```

로 **해시 입력에 들어가며**, `Cogito++_구현명세서.md:1132` 의 `AuditEvent::schema_version = 1` 과 같은 값이다.
즉 **행 단위 payload 스키마 버전**이다. 이것을 DB 스키마 버전으로 오해하고 마이그레이션 때 올리면
**기존 모든 행의 해시가 달라져 `VerifyChain` 이 전부 실패한다.**

### 선택지

| # | 버전 저장 위치 | 장점 | 단점 |
| --- | --- | --- | --- |
| 1 | `PRAGMA user_version` 단독 | SQLite 파일 헤더에 내장. 테이블 존재 여부와 무관하게 **비어 있는/알 수 없는 파일에서도 먼저 읽을 수 있다**. 원자적 | 이력·적용시각·수행 주체를 남길 수 없다. 정수 하나뿐 |
| 2 | `schema_migration` 테이블 단독 | 이력·주체·시각·직전 chain head 를 남긴다 | **"그 테이블이 있는지" 를 알려면 이미 스키마를 알아야 한다.** 미래 버전이 그 테이블을 바꾸면 판정 자체가 불가능 |
| 3 | **둘 다** — `user_version` = 권위, `schema_migration` = 이력 | 1의 판정 가능성과 2의 감사 가능성을 모두 얻는다. 판정(정수 비교)과 기록(행 삽입)의 관심사가 분리된다 | 두 곳이 어긋날 수 있다 → 같은 트랜잭션에서 갱신하는 것이 규범이어야 함 |
| 4 | 파일명·디렉터리에 버전 표기 | 도구 없이 눈으로 확인 | 파일 이동·복사로 조용히 거짓이 된다. 백업 경로에서 특히 위험 |

### 확정

**[S-1] DB 스키마 버전의 권위는 `PRAGMA user_version` 이다.** `Cogito++_구현명세서.md:1887`~`:1934` 의
현행 DDL 전체가 **버전 1** 이다. 새 DB 생성 시 `PRAGMA user_version = 1` 을 같은 트랜잭션에서 설정한다.

**[S-2] `audit_event.schema_version` 은 DB 스키마 버전이 아니다.**
이것은 **해당 행의 `payload_json` 스키마 버전**이며 해시 입력이다(`Cogito++_구현명세서.md:1442`).
두 값은 **독립적으로 증가**한다. §7-4 DDL 에 이 사실을 주석으로 못 박는다.

> 컬럼명을 `payload_schema_version` 으로 개명해도 **해시는 바뀌지 않는다** —
> LP 인코딩(`docs/adr/0004-audit-integrity-and-failure.md:107`)은 **값만** 넣고 컬럼명을 넣지 않는다.
> 그러나 개명은 §4-12 `AuditEvent` 구조체(`Cogito++_구현명세서.md:1132`)와 §6-1 해시 함수,
> C ABI 조회 응답까지 연쇄로 건드린다. **권고는 개명이 아니라 주석 명시다.** 개명을 원하면 S1 이전에 결정해야 한다.

**[S-3] 마이그레이션은 forward-only 다. 하향(다운그레이드)을 금지한다.**

금지 이유는 정책이 아니라 **구조적 불가능**이다.

```
audit_event 는 BEFORE UPDATE / BEFORE DELETE trigger 로 잠겨 있다
   (Cogito++_구현명세서.md:1917~:1923)
→ 다운그레이드가 요구하는 "기존 행 되돌려 쓰기" 는 trigger 가 ABORT 한다
→ trigger 를 지우려면 DROP TRIGGER 가 필요한데 authorizer 가 SQLITE_DROP_TRIGGER 를 거부한다
   (Cogito++_구현명세서.md:1943)
→ 즉 다운그레이드를 "허용" 하려면 append-only 방어 2겹을 먼저 해체해야 한다. 이는 허용할 수 없다
```

**하향이 필요하면 마이그레이션이 아니라 §3 의 복원(restore) 절차를 쓴다.**

**[S-4] 마이그레이션은 additive-only 다.** 허용되는 변경은 다음 세 가지뿐이다.

```
허용 : CREATE TABLE / CREATE INDEX / CREATE TRIGGER / ALTER TABLE ... ADD COLUMN
금지 : 기존 행의 UPDATE·DELETE, 컬럼 삭제·타입 변경·의미 변경,
       ComputeAuditHash 의 입력 필드 집합 변경 (Cogito++_구현명세서.md:1434~:1443)
```

**해시 입력 필드 집합은 마이그레이션으로 바꿀 수 없다.** 새 필드를 해시에 넣으면 기존 행에는
그 필드가 없어 재검증이 전부 실패한다. 새 의미 정보가 필요하면 **`payload_json` 안에 넣고
그 행의 `audit_event.schema_version` 을 올린다** — 그것이 [S-2] 의 두 버전을 분리한 이유다.

> 따라서 **마이그레이션 커넥션도 authorizer 를 완화하지 않는다.**
> `ADD COLUMN`/`CREATE INDEX` 는 `SQLITE_UPDATE`/`SQLITE_DELETE`/`SQLITE_DROP_*` 를 거치지 않으므로,
> `Cogito++_구현명세서.md:1943` 의 거부 목록을 유지한 채 수행할 수 있다.
> **authorizer 를 끄고 마이그레이션하는 구현은 이 계약 위반이다.**
> ⚠ **미확인**: `PRAGMA user_version = N` 쓰기가 authorizer 에 어떤 action code 로 전달되는지,
> `VACUUM` 이 어떤 code 로 보이는지는 확인하지 못했다. **sqlite3 문서로 확인 후 거부/허용 목록을 확정할 것.**

**[S-5] 알 수 없는 상위 버전 DB 는 fail-closed 다.**

| 조건 | 동작 |
| --- | --- |
| `user_version > KNOWN_MAX` | **기동 실패.** agent 를 ready 로 만들지 않는다. `OpsLogger::Critical` 기록 후 종료 |
| `user_version < KNOWN_MAX` | **기동 실패.** 자동 마이그레이션을 하지 않는다. 명시적 CLI 서브커맨드로만 마이그레이션한다 |
| `user_version == KNOWN_MAX` | 정상. 이어서 `VerifyChain()` 을 수행한다 (체크리스트 `:695` — Verify 실패 시 ready 금지) |
| `user_version == 0` 이고 `audit_event` 테이블 부재 | 신규 DB. 버전 1 DDL 을 적용하고 `user_version=1` 설정 |
| `user_version == 0` 이고 `audit_event` 테이블 존재 | **기동 실패.** 버전 표기 없이 데이터가 있는 파일은 출처를 신뢰할 수 없다 |

**상위 버전 DB 에 감사 이벤트를 기록하려 시도해서는 안 된다.** 상위 스키마의 해시 입력 구성이
현재 바이너리와 다를 수 있으므로, "실패를 감사에 남기려는" 선의의 INSERT 가 **체인을 오염시킨다.**
실패 사실은 `OpsLogger`(감사와 분리된 계층 — `include/cogito/ops_log.hpp:7`~`:13`)에만 남긴다.

근거: 불변식 4(판정 불가·감사 실패는 fail-closed, `CLAUDE.md:74`),
`Cogito++_개발_작업체크리스트.md:453` ("schema migration/version mismatch는 자동 추측하지 않고 명확히 실패시킨다"),
`Cogito++_개발_작업체크리스트.md:1717` ("newer DB를 older binary가 임의 downgrade하지 않게 한다").

> **미결**: 기동 실패 시의 프로세스 종료 코드. C ABI 상태 코드 체계(§8-2)와 맞물리므로 **제품 책임자/ABI 담당 결정 필요.**

**[S-6] 마이그레이션 실행 순서 (규범).** 순서를 바꾸면 안전성이 사라진다.

```
① 현재 DB 로 VerifyChain() 전수 성공  ─ 실패하면 마이그레이션을 시작조차 하지 않는다
② 백업 생성 + 백업 파일 SHA-256 + 당시 (last_seq, chain_head) 를 매니페스트에 기록  (§3-확정 [B-2])
③ 단일 write 트랜잭션 안에서:
      additive DDL  →  PRAGMA user_version = N  →  schema_migration INSERT
   (셋 중 하나라도 실패하면 트랜잭션 전체 롤백. 부분 적용 상태를 남기지 않는다)
④ 마이그레이션 후 VerifyChain() 전수 재성공 확인
⑤ 실패 시: 원본 DB 를 그대로 보존하고 agent 를 ready 로 만들지 않는다
```

근거: `Cogito++_개발_작업체크리스트.md:1716`~`:1717`.
④ 를 넣는 이유는 additive-only 라도 **트리거/인덱스 추가가 기존 해시를 건드리지 않았음을 실증**하기 위해서다.

**[S-7] `schema_migration` 도 append-only 다.** 마이그레이션 이력을 지우면 "언제 누가 스키마를 바꿨는가"가
사라진다. `audit_event` 와 동일한 trigger 2개를 건다.

**[S-8] 마이그레이션 사실을 감사 체인에도 남긴다.** `schema_migration` 테이블은 해시체인 밖이므로
그 자체는 변조 탐지 대상이 아니다. 마이그레이션 완료 후 첫 이벤트로
`audit_kind::kAuditRecovery`(`Cogito++_구현명세서.md:1116`)를 payload
`{"action":"schema_migration","from":N-1,"to":N,"backup_sha256":"…"}` 로 커밋한다.
`audit_recovery` 는 `action_id` 가 비어도 되는 이벤트다(`include/cogito/ids.hpp:56`).

### 명세 수정 diff

```diff
 ### 7-4. 감사 DDL (🟡O 반영)

 PRAGMA journal_mode = WAL;
 PRAGMA synchronous  = FULL;
 PRAGMA foreign_keys = ON;
+
+-- [S-1] DB 스키마 버전의 권위. 아래 DDL 전체가 버전 1이다.
+--       audit_event.schema_version 과 혼동하지 말 것 — 두 값은 독립이다.
+PRAGMA user_version = 1;

 CREATE TABLE IF NOT EXISTS audit_event (
   ...
-  schema_version   INTEGER NOT NULL,
+  schema_version   INTEGER NOT NULL,   -- [S-2] 이 행의 payload_json 스키마 버전.
+                                       --       DB 스키마 버전이 아니다 (그것은 PRAGMA user_version).
+                                       --       해시 입력에 포함된다 (§6-1 ComputeAuditHash).
   ...
 );
+
+-- [S-3][S-7] 마이그레이션 이력. forward-only, append-only.
+CREATE TABLE IF NOT EXISTS schema_migration (
+  version           INTEGER PRIMARY KEY,  -- 적용 후의 user_version
+  from_version      INTEGER NOT NULL,
+  applied_at_utc    TEXT    NOT NULL,
+  applied_by        TEXT    NOT NULL,     -- 실행 주체 Subject id. 빈 값 금지
+  tool_version      TEXT    NOT NULL,     -- 수행 바이너리 version + commit
+  chain_head_before BLOB    NOT NULL,     -- ① 단계 VerifyChain 성공 시점의 head (32바이트)
+  last_seq_before   INTEGER NOT NULL,
+  backup_sha256     TEXT    NOT NULL      -- ② 단계 백업 파일 digest
+);
+
+CREATE TRIGGER IF NOT EXISTS schema_migration_no_update
+BEFORE UPDATE ON schema_migration
+BEGIN SELECT RAISE(ABORT, 'schema_migration is append-only'); END;
+
+CREATE TRIGGER IF NOT EXISTS schema_migration_no_delete
+BEFORE DELETE ON schema_migration
+BEGIN SELECT RAISE(ABORT, 'schema_migration is append-only'); END;
```

### 영향

- **체크리스트 S5-06** (`:669`~`:676`): "migration 001" 이 이제 정의된다 — §7-4 DDL + `user_version=1`.
  `:676` 의 5개 테스트(중/실패/재실행/낮은 버전/높은 버전)가 [S-5] 표와 1:1 대응한다
- **체크리스트 S11-08** (`:1715`~`:1717`): [S-1]·[S-3]·[S-6] 이 그대로 대응한다
- **Codex 인계**: `src/audit_sqlite/sqlite_audit_journal.cpp`(버전 판정·기동 실패 경로),
  신규 `migration.cpp`. **authorizer 를 끄지 않는다**는 [S-4] 단서를 반드시 전달할 것
- `tests/audit/` 에 `migration` 케이스가 추가된다 (§10-2 표에 현재 없음 — 추가 필요)

---

## 2. WAL 임계값 · 디스크 여유

### 모순

| 위치 | 서술 |
| --- | --- |
| `Cogito++_구현명세서.md:2791` | "`db-wal` 크기·디스크 여유 감시. 임계 초과 시 **감사 조회를 먼저 차단하고 write 실행 경로를 보존**한다" |
| `Cogito++_구현명세서.md:2651` (W17) | 장시간 읽기 트랜잭션이 "WAL 체크포인트를 막아 디스크를 채우고, 그 결과 §6-2-a의 사전 감사 커밋이 실패해 **불변식 8에 의해 설비 조작이 정지**한다" |
| `Cogito++_구현명세서.md:1791`~`:1794` (§7-1 `audit` 블록) | `db_path`, `read_fail_closed`, `anchor` **뿐.** 임계값 필드가 없다 |
| `Cogito++_개발_작업체크리스트.md:1722` | "disk free/WAL threshold alert와 audit read shedding을 운영 test한다" |

**임계값의 숫자도, 감시 주체도, 감시 주기도, 임계 도달 시의 정확한 동작 순서도 없다.**
W17 은 위험을 정확히 서술해 놓고 **대응은 §12-5 의 한 줄에 맡겼는데 그 줄에도 숫자가 없다.**

### 선택지

| # | 임계 도달 시 동작 | 판정 |
| --- | --- | --- |
| 1 | **디스크가 차면 감사를 건너뛰고 실행은 계속** | **절대 불가.** 불변식 4(`CLAUDE.md:74`)·불변식 8(`CLAUDE.md:78`)·`Cogito++_구현명세서.md:2505`(`audit/failure` = "감사 DB 잠금·디스크 부족·해시 실패 시 write 호출 0회")·`Cogito++_구현_요구사항.md:115` 를 정면으로 위반한다. **선택지로 존재하지 않는다** |
| 2 | 감사 이벤트를 메모리에 버퍼링하고 handler 는 먼저 호출 | **불가.** 불변식 7(write 전에 verdict 와 `tool_call_started` 가 **내구성 있게** 커밋)이 깨진다. 메모리 버퍼는 내구성이 아니다 |
| 3 | 감사 이벤트를 요약·절단해 크기를 줄인다 | **불가.** payload 가 바뀌면 해시가 바뀐다. 그리고 "언제 요약했는가"가 다시 감사 대상이 된다 |
| 4 | 임계 도달 시 **감사 조회(읽기)를 먼저 죽이고 쓰기 경로를 보존**, 그래도 안 되면 **새 작업 자체를 거부** | 명세 `:2791` 의 서술과 일치. 읽기는 안전하게 실패시킬 수 있지만 쓰기는 그렇지 않다 |
| 5 | 임계 도달 시 즉시 프로세스 중단 | 단순하지만 과하다. 진행 중인 턴의 `turn_end` 조차 기록하지 못해 §6-3 의 `finalize_pending` 탈출구를 잃는다 |

### 확정

**[W-1] 선택지 1·2·3 은 영구 금지다.** 어떤 임계 상태에서도 **"감사 없이 handler 를 호출하는 경로"는 존재하지 않는다.**
디스크 압박은 **가용성 문제**이지 안전 요구사항의 예외 사유가 아니다.
이 문장을 §7-4 또는 §12-5 에 명문화한다.

**[W-2] 3단계 상태기를 둔다.** 상태 전이는 코어의 `AuditJournal` 구현(단일 writer)이 소유한다.

| 상태 | 진입 조건 | 동작 |
| --- | --- | --- |
| `Normal` | 임계 미달 | 평시 |
| `ReadShedding` | 디스크 여유 < `disk_warn` **또는** `-wal` 크기 > `wal_warn` | ① `cogito_query_audit` 를 오류로 거부(§12-5 강제조건 5 그대로) ② 진행 중 읽기 트랜잭션을 2초 상한으로 강제 종료(§12-5 강제조건 3) ③ `wal_checkpoint(PASSIVE)` → 실패 시 `(TRUNCATE)` 시도 ④ `OpsLogger::Warn` |
| `WriteCritical` | 디스크 여유 < `disk_critical` **또는** `-wal` 크기 > `wal_critical` | ① 새 턴 시작 거부 ② `effect=write`/`destructive` 도구를 Gate 에서 Deny ③ **감사 커밋은 계속 시도한다** ④ `OpsLogger::Critical` |

**해제**는 임계 아래로 내려간 뒤 이력(hysteresis)을 두고 수행한다 — 임계선에서 진동하면 조회가 깜빡인다.
**이력 폭도 미결값이다.**

**[W-3] 상태와 무관하게, 감사 커밋이 실제로 실패하면 기존 경로를 그대로 탄다.**
새 경로를 만들지 않는다.

```
사전 감사 커밋 실패        -> handler 호출 0회 (불변식 8)
turn_end 커밋 실패         -> finalize_pending_ = true
                              -> RetryFinalize() / SealSession()   (Cogito++_구현명세서.md:1617~:1687)
봉인마저 실패              -> Critical 기록 후 프로세스 중단        (Cogito++_구현명세서.md:1680~:1684)
```

즉 **[W-2] 는 "실패를 늦추기 위한 완충"이지 "실패를 우회하는 길"이 아니다.**

**[W-4] 감시 방법.**

```
디스크 여유 : DB 파일이 위치한 볼륨. C++17 std::filesystem::space() 로 조회 가능
WAL 크기    : DB 경로의 "-wal" 파일 크기 (§12-5 강제조건 5 가 "db-wal" 이라 부르는 그것)
감시 시점   : 매 커밋 시도 직전은 비용이 크다. 최소 (a) 프로세스 기동 시,
              (b) N 커밋마다, (c) ReadShedding 진입 후에는 매 커밋마다 로 단계화한다
```

`N` 값 미결. **비용을 재지 않고 "매 커밋마다 stat 해도 무시할 만하다"고 쓰지 않는다**(부록 A 마지막 항목 — 검증되지 않은 수치 금지).

**[W-5] WAL 이 커지는 주 원인은 쓰기량이 아니라 읽기다.**
감사 저널은 **단일 writer**(체크리스트 `:682` — "single writer를 mutex/owner thread로 보장")이므로
쓰기 경합으로 WAL 이 폭증하지 않는다. 폭증은 **checkpoint 를 막는 장시간 읽기 트랜잭션**에서 온다(W17, `:2651`).
따라서 §12-5 강제조건 3(페이지 200행 / 동시 조회 2건 / 읽기 트랜잭션 2초 상한)은
**성능 제한이 아니라 가용성 안전장치**다. 그 사실을 §12-5 에 명시한다.
같은 상한을 **코어 자신의 읽기 커넥션(`PRAGMA query_only=ON`, `:2788`)에도 동일 적용**한다.

**[W-6] `wal_autocheckpoint` 는 명시적으로 설정한다.**
⚠ **SQLite 의 기본 `wal_autocheckpoint` 값과 그 단위(페이지 수)는 확인하지 못했다.**
`Cogito++_구현명세서.md:2344` 의 `builtin-baseline` 이 아직 `REPLACE_WITH_VCPKG_COMMIT_SHA` 이므로
**사용할 sqlite3 포트 버전조차 확정되지 않았다.**
버전 확정 후 그 버전의 문서로 기본값을 확인하고, **기본값 의존 없이 설정값을 명시**한다.
**어떤 SQLite 버전·컴파일 옵션을 쓸지는 미결이며 이 문서가 정하지 않는다.**

### 명세 수정 diff

```diff
 ### 7-1. `config/cogito.json` (🟡N 반영)

   "audit": {
     "db_path": "audit.db",
     "read_fail_closed": true,
-    "anchor": { "enabled": false, "path": "anchor/chain-head.json", "interval_events": 500 }
+    "anchor": { "enabled": false, "path": "anchor/chain-head.json", "interval_events": 500 },
+
+    // [W-2] 용량 임계. 아래 5개 값은 전부 미결 — 제품 책임자 결정 필요.
+    //       스키마는 고정하되 값을 확정하기 전에는 부팅을 실패시킨다(임의 기본값 금지).
+    "capacity": {
+      "disk_warn_bytes":     null,   // 미결
+      "disk_critical_bytes": null,   // 미결
+      "wal_warn_bytes":      null,   // 미결
+      "wal_critical_bytes":  null,   // 미결
+      "check_every_commits": null,   // 미결 (W-4 의 N)
+      "hysteresis_ratio":    null    // 미결
+    },
+    "wal_autocheckpoint_pages": null // 미결 (W-6). SQLite 기본값에 의존하지 않는다
   },
```

```diff
 ### 7-4. 감사 DDL (🟡O 반영)
+
+**용량 압박 시의 금지 사항 (불변식 4·8)**
+
+디스크 부족·WAL 팽창·DB 잠금은 **감사를 생략할 사유가 되지 않는다.**
+다음은 어떤 임계 상태에서도 금지한다.
+  - 감사 이벤트를 건너뛰고 Tool handler 를 호출하는 것
+  - 감사 이벤트를 메모리에만 두고 handler 를 먼저 호출하는 것 (불변식 7 — 내구성 필요)
+  - 크기를 줄이려고 payload 를 요약·절단하는 것 (해시가 달라진다)
+감사 커밋이 실패하면 write 실행 횟수는 0 이다. 임계 대응은 이 실패를 **늦추기 위한 것**이며
+**우회하기 위한 것이 아니다.**
```

```diff
 **`GET /api/audit` 강제 조건** (하나라도 없으면 경로를 제공하지 않는다)
-3. 페이지 상한 200행, 동시 조회 2건, 단일 읽기 트랜잭션 2초 상한
+3. 페이지 상한 200행, 동시 조회 2건, 단일 읽기 트랜잭션 2초 상한.
+   이 셋은 성능 제한이 아니라 **가용성 안전장치**다 — 장시간 읽기 트랜잭션이 WAL checkpoint 를
+   막으면 디스크가 차고, 그 결과 사전 감사 커밋이 실패해 불변식 8로 설비 조작이 정지한다(W17).
+   코어 자신의 읽기 커넥션에도 동일 상한을 적용한다.
-5. `db-wal` 크기·디스크 여유 감시. 임계 초과 시 **감사 조회를 먼저 차단하고 write 실행 경로를 보존**한다
+5. `db-wal` 크기·디스크 여유 감시. 임계 초과 시 **감사 조회를 먼저 차단하고 write 실행 경로를 보존**한다.
+   상태기·임계 필드·금지 사항은 §7-1 `audit.capacity` 와 §7-4 를 따른다
```

### 영향

- **§10-2 테스트 표**(`Cogito++_구현명세서.md:2505`)의 `audit/failure` 에 케이스 추가:
  `ReadShedding` 진입 시 조회 거부 · `WriteCritical` 에서 write 도구 Deny ·
  **두 상태 모두에서 `tool_call_started` 커밋이 성공하거나 실패하면 write 0회**
- **체크리스트 S11-08** `:1722` 가 그대로 대응한다
- **§12-13 `web/audit_readpath`**(`:3021`)와 겹친다 — 웹 담당과 값 공유 필요
- **Codex 인계**: `src/audit_sqlite/sqlite_audit_journal.cpp`.
  **임계값이 `null` 이면 부팅 실패시킬 것.** 코드에 기본값을 넣지 말 것

---

## 3. 보존 · 백업 · 복구

### 모순

| 위치 | 서술 |
| --- | --- |
| `Cogito++_구현_요구사항.md:420` | "DB, WAL, 백업을 동일한 보안·보존 정책으로 관리한다" |
| `Cogito++_구현_요구사항.md:624` | 테스트 요구: "WAL checkpoint, 백업, 복원, 보존기간 테스트" |
| `Cogito++_구현_요구사항.md:702` | 미결: "감사 보존기간, 암호화 키, 외부 앵커 대상, 장애 시 read 허용 여부" |
| `Cogito++_구현명세서.md:3032` | 미결: "`audit_reader`가 볼 수 있는 보존 기간" |
| `Cogito++_개발_작업체크리스트.md:1721` | "**retention/purge가 append-only 계약과 충돌하므로** archive/anchor/새 chain 시작 절차를 ADR로 정한다" |
| `Cogito++_구현명세서.md` §7-4 · §7-5 | **보존·백업·복구 절차가 한 줄도 없다.** `chain_anchor` 테이블(`:1926`~`:1931`)만 "(선택)" 으로 존재 |

**근본 모순은 체크리스트 `:1721` 이 정확히 짚었다** — 보존 기간이 지난 행을 **지울 수 없다.**
`BEFORE DELETE` trigger 가 ABORT 하고(`:1921`~`:1923`), authorizer 가 `SQLITE_DELETE` 를 거부한다(`:1943`).
따라서 **"보존 기간" 을 "그 이후 삭제" 로 구현할 방법이 없다.**

### 선택지

**보존(purge) 방식**

| # | 안 | 판정 |
| --- | --- | --- |
| 1 | 기간 경과 행을 DELETE | **불가.** trigger·authorizer 가 막는다. 뚫으려면 append-only 방어를 해체해야 한다 |
| 2 | DB 파일을 통째로 삭제하고 새로 시작 | 체인 연속성이 사라진다. "그 기간에 무슨 일이 있었는가"에 답할 수 없고, 삭제 사실 자체가 기록되지 않는다 |
| 3 | **파일 단위 아카이브 + 체인 승계(rollover)** | 삭제가 아니라 **분리**다. 옛 체인은 봉인된 채 온전히 남고, 새 체인이 옛 체인을 가리킨다. 보존 기간은 "온라인 조회 가능 기간"으로 재정의된다 |
| 4 | 오래된 payload 만 마스킹해 크기를 줄인다 | **불가.** payload 변경 = 해시 변경 = 체인 파괴. §7-5 의 마스킹은 **저장 전** 단계다 |

**새 체인의 `prev_hash` 최초값**

| # | 안 | 판정 |
| --- | --- | --- |
| A | 이전 파일의 `head_hash` 를 이어붙인다 | 두 파일을 물리적으로 연결. 단, 검증기가 **항상 모든 아카이브를 갖고 있어야** 새 파일을 검증할 수 있다. ADR-0004 D9 (`:160` — "최초값은 문서화된 32바이트 zero")와 충돌 |
| B | **32바이트 zero 유지 + 첫 이벤트로 이전 체인을 참조** | D9 를 건드리지 않는다. 새 파일은 독립적으로 검증 가능하고, 연결은 **첫 이벤트의 payload** 가 증언한다 |

**백업 방식**

| # | 안 | 판정 |
| --- | --- | --- |
| P | 실행 중 `.db` 파일만 복사 (`cp`/`robocopy`) | **금지.** WAL 모드에서 `.db` 단독 복사는 미체크포인트 트랜잭션을 잃는다. 조용히 손상된 백업이 생긴다 |
| Q | 프로세스 정지 → `wal_checkpoint(TRUNCATE)` → `.db` 복사 | 단순·확실. 다운타임 필요 |
| R | SQLite Online Backup API | 무중단. 구현 필요 |
| S | `VACUUM INTO` | 무중단이고 파일이 조밀해진다. ⚠ **`VACUUM` 이 `INTEGER PRIMARY KEY AUTOINCREMENT` 의 `seq` 값을 보존하는지 확인하지 못했다.** `seq` 는 전역 순서 권위(`:1937`)이므로 보존되지 않으면 **채택 불가** |

### 확정

**[B-1] 보존은 삭제가 아니라 아카이브다 — 선택지 3.**

"보존 기간" 의 의미를 다음 둘로 **분리해 정의**한다.

```
온라인 보존 기간  : 현재 audit.db 에 남아 있어 cogito_query_audit 로 조회 가능한 기간
아카이브 보존 기간 : 봉인된 아카이브 파일을 보관하는 기간. 조회는 오프라인 도구로만
```

**두 값 모두 미결이며 제품 책임자가 정한다**(`Cogito++_구현_요구사항.md:702`, `Cogito++_구현명세서.md:3032`).
법령·고객 요구가 결정하는 값이므로 **AI 가 제안값을 확정으로 쓰지 않는다.**

**[B-2] Rollover(체인 승계) 절차.**

```
① VerifyChain() 전수 성공                  ─ 실패하면 rollover 하지 않는다
② wal_checkpoint(TRUNCATE) 로 WAL 을 DB 에 흡수
③ 현재 파일을 아카이브 경로로 이동하고 읽기 전용 권한으로 봉인
④ 아카이브 매니페스트 기록:
      { archive_path, sha256, last_seq, head_hash, sealed_at_utc, sealed_by }
⑤ 새 audit.db 생성 (user_version = 현재 KNOWN_MAX, §7-4 DDL)
⑥ 새 체인의 첫 이벤트를 커밋한다 — prev_hash 는 32바이트 zero (선택지 B, ADR-0004 D9 유지)
      kind    = audit_kind::kAuditRecovery
      payload = { "action": "chain_rollover",
                  "prev_archive_sha256": …, "prev_last_seq": …, "prev_head_hash": …,
                  "reason": "retention" | "size" | "migration" }
```

`audit_recovery` 를 재사용하고 새 kind 를 만들지 않는 이유:
`Cogito++_구현명세서.md:1673`~`:1676` 의 `SealSession()` 이 이미
`{"action":"seal_session", …}` 패턴으로 payload 의 `action` 필드로 구분한다.
새 kind 를 추가하면 §4-12 `audit_kind`(`:1105`~`:1118`)와 `include/cogito/ids.hpp:56` 의
"`action_id` 가 비어도 되는 이벤트" 목록을 **둘 다** 바꿔야 한다. `audit_recovery` 는 이미 그 목록에 있다.

> ⚠ **선택지 B 의 한계를 정직하게 적는다.** 새 파일 하나만으로는
> "이전 체인이 실제로 그 head 로 끝났는지" 를 **증명할 수 없다.** 첫 이벤트는 *주장* 이다.
> 그 주장은 (i) 아카이브 파일 자체, (ii) 외부 앵커(§7-1 `anchor` 블록, `:1793`),
> (iii) 매니페스트 서명이 함께 있어야 검증된다.
> **"rollover 가 체인 연속성을 보장한다" 고 쓰지 말 것.** 정확한 표현은
> **"rollover 는 이전 체인의 종단을 새 체인의 첫 행에 기록하며, 실제 대조는 아카이브 파일과 앵커로 한다"** 이다.

**[B-3] 백업 방식은 Q 또는 R 중 하나만 쓴다. P 는 금지한다.**

```
허용 : (Q) 프로세스 정지 → wal_checkpoint(TRUNCATE) → .db 복사
       (R) SQLite Online Backup API
금지 : (P) 실행 중 임의 파일 복사 — 조용히 손상된 백업을 만든다
보류 : (S) VACUUM INTO — seq 보존 여부 미확인. 확인 전 채택 금지
```

`Cogito++_개발_작업체크리스트.md:1719` ("WAL 포함 online backup 또는 승인된 shutdown backup 절차를 정한다")과 일치한다.
**모든 백업 산출물에 매니페스트를 함께 만든다** — `sha256`, `last_seq`, `chain_head`, 생성 시각, 생성 주체.

**[B-4] 백업의 무결성 정의.** "백업이 유효하다" 는 다음 세 조건을 모두 만족할 때만 참이다.

```
① 백업 파일의 SHA-256 이 매니페스트와 일치
② 백업 파일에서 VerifyChain() 이 전수 성공
③ 백업 파일의 chain_head 와 last_seq 가 매니페스트와 일치
```

백업은 체인을 **바꾸지 않는다.** 백업이 체인 연속성을 "보장" 하는 것이 아니라,
**체인이 백업의 무결성을 검증할 수단을 제공**하는 것이다. 방향을 뒤집어 쓰지 말 것.

**[B-5] 복구(restore) 절차.** 복구는 **다운그레이드 경로가 아니다**([S-3]).

```
① 복원 파일 SHA-256 을 매니페스트와 대조                  ─ 불일치 시 중단
② PRAGMA user_version 검사 ([S-5] 표 그대로 적용)          ─ 상위 버전이면 여전히 기동 실패
③ VerifyChain() 전수                                       ─ 실패 시 ready 금지 (체크리스트 :695)
④ RecoverDangling() (§4-12, Cogito++_구현명세서.md:1142~:1144)
      tool_call_started 만 있고 tool_result 가 없는 항목에
      indeterminate 결과 + audit_recovery 를 생성한다
⑤ 복원 사실 자체를 체인에 남긴다
      kind    = audit_kind::kAuditRecovery
      payload = { "action": "restore", "source_sha256": …, "source_last_seq": …,
                  "restored_by": …, "restored_at_utc": … }
⑥ ProcessEpochId() 는 새 프로세스이므로 새 값이다 (include/cogito/ids.hpp:66~:68)
      → 웹 클라이언트는 재생하지 않고 전체 상태를 다시 조회한다 (§12-12 "재접속" 행)
      → monotonic_ns 비교는 epoch 경계를 넘어 수행하지 않는다 (:1937~:1939)
```

**[B-6] 복구가 만든 `indeterminate` 는 lockdown 을 다시 켜야 한다. (안전 결정)**

`Cogito++_구현명세서.md:1703`~`:1737` 의 §6-4 는 `indeterminate` 발생 시
`line_write_lockdown_ = true` 로 두고 **`operator_ack` 가 올 때까지 유지**한다고 규정한다.
그런데 `Cogito++_구현명세서.md:1259`~`:1260` 을 보면 `indeterminate_lock_`(`std::set<std::string>`)과
`line_write_lockdown_`(`bool`)은 **`AgentLoop` 의 멤버 변수**다. 즉 **프로세스 메모리 상태**이고 DB 에 없다.

```
크래시 → 재시작 → RecoverDangling() 이 indeterminate 를 생성
       → 그런데 새 프로세스의 line_write_lockdown_ 은 false 로 시작한다
       → "설비 상태를 확인할 수 없다" 는 사실이 살아 있는데 write 잠금은 풀려 있다
```

**즉 프로세스 재시작이 §6-4 의 안전 통제를 조용히 해제한다.**
`RecoverDangling()` 이 존재하는 이유(`:1142`~`:1144`)를 생각하면 이는 명백한 결함이다.

**확정**: 기동 시 감사 DB 를 조회해 **"마지막 `indeterminate`/`audit_recovery` 이후
그것을 해제하는 `operator_ack`(`audit_kind::kOperatorAck`, `:1117`)이 존재하는가"** 를 판정한다.
없으면 **`line_write_lockdown_` 을 켠 상태로 기동**한다. 해제는 `operator_ack` 로만 가능하다
(§6-4 의 `AcknowledgeIndeterminate`, `:1737`~`:1739`).

> **미결**: `operator_ack` payload 가 **무엇을 해제하는지** 를 어떻게 표현할지가 §6-4 에 없다.
> `operation_digest` 단위인지(ADR-0004 D7, `:136`), 라인 단위인지, 전역인지.
> 현재 `indeterminate_lock_`(세션 내 키 집합)과 `line_write_lockdown_`(불리언)이 **해제 단위가 다르다.**
> **이 결정은 §6-4 담당 범위이며 이 문서가 정하지 않는다.**

**[B-7] 아카이브·백업 파일은 원본과 동일한 보안 등급으로 다룬다.**
`Cogito++_구현_요구사항.md:420` 이 이미 요구한다. 구체적으로:
`.db` · `-wal` · `-shm` · 아카이브 · 백업 · 매니페스트 전부.
`Cogito++_구현명세서.md:1943` 이 지적하듯 **authorizer 는 로컬 관리자를 막지 못하므로**,
파일 권한/ACL·암호화·오프사이트 보관이 실질 방어다.
**암호화 방식·키 관리는 미결**(`Cogito++_구현_요구사항.md:702`).

### 명세 수정 diff

```diff
 ### 7-5. 민감정보 취급
 …
 원문 보존이 필요한 배포는 별도 암호화 blob·키 관리·역할 기반 조회·보존기간·삭제 절차를 정의한 뒤에만 활성화한다.
+
+### 7-6. 보존 · 아카이브 · 백업 · 복구 (신설)
+
+**보존은 삭제가 아니다.** `audit_event` 는 trigger·authorizer 로 DELETE 가 차단되므로(§7-4),
+기간 경과 행을 지우는 purge 는 구현할 수 없다. 보존은 **파일 단위 아카이브 + 체인 승계**로 정의한다.
+
+| 용어 | 정의 |
+| --- | --- |
+| 온라인 보존 기간 | 현재 `audit.db` 에 남아 `cogito_query_audit` 로 조회 가능한 기간. **값 미결** |
+| 아카이브 보존 기간 | 봉인된 아카이브 파일의 보관 기간. 오프라인 도구로만 조회. **값 미결** |
+
+**Rollover**: VerifyChain → checkpoint(TRUNCATE) → 파일 봉인 → 매니페스트 → 새 DB →
+첫 이벤트 `audit_recovery{action:"chain_rollover", prev_*}`. 새 체인의 `prev_hash` 최초값은
+32바이트 zero 를 유지한다(ADR-0004 D9). 이전 체인과의 대조는 아카이브 파일과 외부 앵커로 한다 —
+**rollover 자체가 연속성을 보장하지 않는다.**
+
+**백업**: (a) 정지 후 `wal_checkpoint(TRUNCATE)` → `.db` 복사, 또는 (b) SQLite Online Backup API.
+**실행 중 임의 파일 복사는 금지한다.** 산출물마다 `{sha256, last_seq, chain_head, 시각, 주체}` 매니페스트를 만든다.
+
+**복구**: 매니페스트 대조 → `user_version` 검사(§7-4 [S-5]) → `VerifyChain()` 전수 →
+`RecoverDangling()` → `audit_recovery{action:"restore", …}` 커밋 → 새 `process_epoch_id`.
+**복구 시 해제되지 않은 `indeterminate` 가 있으면 `line_write_lockdown_` 을 켠 상태로 기동한다**(§6-4).
+
+`.db` · `-wal` · `-shm` · 아카이브 · 백업 · 매니페스트는 **모두 동일한 보안·보존 정책**으로 관리한다.
```

```diff
 ### 12-14. 미결 — 제품 책임자 결정 (§16에 추가)
 - `audit_reader`가 볼 수 있는 보존 기간
+- 온라인 보존 기간 / 아카이브 보존 기간 (§7-6). 두 값은 다르다
+- 아카이브·백업의 보관 위치, 오프사이트 여부, 암호화 방식과 키 관리
+- 백업 주기와 백업 수행 주체
```

> ⚠ **부수 발견**: `Cogito++_구현명세서.md:3025` 는 미결을 "**§16에 추가**" 하라고 하는데,
> 명세서의 절 구성은 `§1`~`§13` + `부록 A` + `부록 B` 이며 **§14·§15·§16 이 존재하지 않는다.**
> 즉 **미결 항목을 모을 곳이 명세에 없다.** 별도 정정이 필요하다(이 문서 범위 밖).

### 영향

- **체크리스트 S11-08** `:1718`~`:1721` 전부 대응. 특히 `:1721` 이 요구한 "archive/anchor/새 chain 시작 절차" 가 [B-2]
- **체크리스트 S11-09 운영 Runbook** `:1730` ("audit DB lock/disk full/WAL growth/chain tamper 대응"),
  `:1731` ("process epoch 변경 뒤 client full refresh와 replay 금지") 가 [W-2]·[B-5]⑥ 과 직결
- **§10-2 `audit/recovery`**(`:2504`)에 케이스 추가: rollover 후 새 체인 검증 · 복구 후 lockdown ON
- **§4-12 `AuditJournal`** 에 rollover/backup 진입점이 필요한가는 미결 —
  현재 인터페이스는 `Commit`/`VerifyChain`/`RecoverDangling`/`chain_head` 4개뿐(`:1136`~`:1145`).
  **오프라인 CLI 도구로 뺄지 저널 인터페이스에 넣을지 결정 필요**
- **Codex 인계**: `src/audit_sqlite/recovery.cpp`. **[B-6] 의 "복구 후 lockdown ON" 을 반드시 전달할 것**

---

## 4. append-only 는 무엇이 강제하는가 (표기 정확성)

### 모순

`CLAUDE.md:110` 과 `Cogito++_구현명세서.md:3056` 은 다음을 **금지 표현**으로 못 박았다.

```
✗ "WAL이 append-only를 보장한다"
→ "trigger와 authorizer로 UPDATE/DELETE를 차단하고 해시체인으로 훼손을 탐지한다"
```

`Cogito++_구현_요구사항.md:37` 도 같은 항목을 **"오류"** 로 분류한다.
그럼에도 이 주제는 문서 곳곳에 흩어져 있어 **한 곳에서 정확히 읽을 수 없다.**
§7-4 DDL 주석(`:1916`)과 authorizer 문단(`:1943`)에 나뉘어 있고,
**무엇을 막지 *못하는지*** 는 `:1943` 한 문장에만 있다.

### 확정

**[A-1] 4개 층의 역할을 분리해 서술한다. 층마다 막는 것과 못 막는 것이 다르다.**

| 층 | 기제 | 근거 | 막는 것 | **막지 못하는 것** |
| --- | --- | --- | --- | --- |
| 1. 예방(DB 내부) | `BEFORE UPDATE` / `BEFORE DELETE` trigger → `RAISE(ABORT)` | `Cogito++_구현명세서.md:1917`~`:1923` | 같은 DB 를 여는 **모든** SQL 경로의 UPDATE/DELETE | `DROP TRIGGER` 후의 조작. 파일 직접 편집. 파일 교체 |
| 2. 예방(프로세스 내) | `sqlite3_set_authorizer` — `SQLITE_UPDATE`/`SQLITE_DELETE`/`SQLITE_DROP_TABLE`/`SQLITE_DROP_TRIGGER` 거부 | `Cogito++_구현명세서.md:1943` | **우리 프로세스 안의** 실수·버그·주입된 SQL | **다른 프로세스**(`sqlite3` CLI 등)는 우리 authorizer 를 쓰지 않는다. 로컬 관리자 |
| 3. 탐지 | SHA-256 해시체인 재계산 (`VerifyChain`) | `:1434`~`:1443`, ADR-0004 D9 (`:158`~`:168`) | 필드 변조·payload 변조·중간 행 삭제·순서 변경·head 변경 (`:2502`) | **예방하지 못한다.** 사후 탐지다. 그리고 **파일 전체를 일관되게 재생성한 위조는 탐지하지 못한다** |
| 4. 외부 | 파일 권한/ACL, 외부 앵커(§7-1 `anchor`), 오프사이트 백업, 서명 | `:1943`, `:1793` | 3층이 못 잡는 **일관된 전체 재생성** | 앵커/백업 자체가 같은 권한으로 조작 가능하면 무력 |

**WAL 은 이 네 층 어디에도 없다.** WAL 은 `journal_mode` 로서 **내구성·동시성 특성**이며
무결성 통제가 아니다(`Cogito++_구현명세서.md:1916` 이 이미 이렇게 적어 두었다).
`synchronous = FULL`(`:1891`)이 함께 있어야 "커밋된 판정은 정전 후에도 남는다" 가 성립한다.

**[A-2] 정확한 문장 (문서·UI·발표에 이대로 쓴다).**

> `audit_event` 는 **trigger 와 authorizer 두 겹으로 UPDATE/DELETE 를 차단**하고,
> **SHA-256 해시체인으로 훼손을 탐지**한다.
> 두 예방 층은 **프로세스 내부의 사고**를 막는 것이며,
> **로컬 관리자 권한에 대한 변조 방지가 아니다.**
> 그것은 파일 권한·암호화·외부 앵커링·오프사이트 백업이 함께 있어야 한다.

**과장하지 말아야 할 표현 3개** (부록 A 후보로 추가 제안):

| ✗ 쓰지 않는다 | ✓ 대신 |
| --- | --- |
| "감사 DB 는 변조 불가능하다" | "프로세스 내 UPDATE/DELETE 를 차단하고, 파일 수준 훼손은 해시체인으로 탐지한다" |
| "백업이 있으므로 감사 기록은 안전하다" | "백업은 가용성 수단이다. 무결성은 VerifyChain 과 매니페스트 대조로 확인한다" |
| "rollover 가 체인 연속성을 보장한다" | "rollover 는 이전 체인의 종단을 새 체인 첫 행에 기록한다. 대조는 아카이브 파일과 앵커로 한다" |

**[A-3] 마이그레이션·백업·복구 경로에서도 authorizer 를 끄지 않는다.**
[S-4] 의 additive-only 정책이 이것을 가능하게 한다. **"마이그레이션을 위해 잠깐 푼다" 는 없다.**

**[A-4] 운영 권한 요구.** `Cogito++_개발_작업체크리스트.md:675` 는
"application DB user가 trigger/authorizer를 제거하지 못하는 운영 권한을 정의한다" 를 요구한다.
SQLite 는 DB 사용자 개념이 없으므로 **이는 OS 파일 권한 문제로 환원된다.**
`audit.db` 를 쓰는 OS 계정과 그 파일을 관리하는 계정을 분리해야 한다.
**구체적 계정·ACL 모델은 미결이며 배포 환경(Windows 패널 PC / Linux 엣지)에 따라 다르다 — 제품 책임자 결정 필요.**

### 명세 수정 diff

```diff
 -- append-only 강제. WAL 은 내구성 모드일 뿐 이 제약을 주지 않는다.
```
(위 주석은 이미 정확하다. 유지한다.)

```diff
 추가로 `sqlite3_set_authorizer`로 `SQLITE_UPDATE` / `SQLITE_DELETE` / `SQLITE_DROP_TABLE` / `SQLITE_DROP_TRIGGER`를 `audit_event`에 대해 거부한다. 이는 **프로세스 내 사고 방지**이지 로컬 관리자에 대한 변조 방지가 아니다 — 파일 권한·암호화·백업·외부 앵커링이 함께 필요하다.
+
+**append-only 를 강제하는 것과 강제하지 못하는 것 (4개 층)**
+
+| 층 | 기제 | 막는 것 | 막지 못하는 것 |
+| --- | --- | --- | --- |
+| 1 예방(DB) | BEFORE UPDATE/DELETE trigger | 모든 SQL 경로의 UPDATE/DELETE | DROP TRIGGER 이후, 파일 직접 편집·교체 |
+| 2 예방(프로세스) | `sqlite3_set_authorizer` | 우리 프로세스 내부의 사고·주입 | 다른 프로세스, 로컬 관리자 |
+| 3 탐지 | SHA-256 해시체인 (`VerifyChain`) | 필드·payload 변조, 행 삭제, 순서·head 변경 | 예방은 못 한다. 파일 전체를 일관되게 재생성한 위조 |
+| 4 외부 | 파일 권한/ACL, 외부 앵커, 오프사이트 백업, 서명 | 3층이 못 잡는 전체 재생성 | 앵커·백업이 같은 권한으로 조작되면 무력 |
+
+**WAL 은 위 네 층 어디에도 속하지 않는다.** `journal_mode` 는 내구성·동시성 특성이다.
+마이그레이션·백업·복구 경로에서도 authorizer 를 완화하지 않는다 — 마이그레이션은 additive-only 이므로
+`SQLITE_UPDATE`/`SQLITE_DELETE`/`SQLITE_DROP_*` 를 필요로 하지 않는다.
```

### 영향

- **§10-2 `audit/authorizer`**(`:2503`)에 케이스 추가:
  **마이그레이션 커넥션에서도 UPDATE/DELETE/DROP 이 거부된다** · 백업·복구 경로 동일
- **부록 A**(`:3048`~)에 [A-2] 의 금지 표현 3개 추가 제안
- **체크리스트 S5-06** `:675` 의 "운영 권한 정의" 는 [A-4] 로 **OS 권한 모델 미결**로 승격된다

---

## 5. 상호 정합성 확인

| 쌍 | 확인 |
| --- | --- |
| §1 [S-4] ↔ §4 [A-3] | additive-only 이므로 마이그레이션이 `SQLITE_UPDATE/DELETE/DROP_*` 를 요구하지 않는다 → authorizer 를 끌 이유가 없다. **두 결정이 서로를 성립시킨다** |
| §1 [S-4] ↔ ADR-0004 D9 | 해시 입력 필드 집합(`:1434`~`:1443`)이 고정되므로 `ADD COLUMN` 이 기존 행 검증을 깨지 않는다 |
| §1 [S-2] ↔ ADR-0004 D5 | LP 인코딩(`docs/adr/0004-audit-integrity-and-failure.md:107`)은 **값만** 넣으므로 컬럼 개명이 해시를 바꾸지 않는다 |
| §2 [W-3] ↔ §6-3 | 임계 대응은 새 실패 경로를 만들지 않고 `finalize_pending_`/`SealSession()`(`:1617`~`:1687`)에 합류한다 |
| §2 [W-5] ↔ §12-5 W17 | 읽기 상한이 WAL 팽창을 막고, WAL 팽창이 불변식 8 정지를 부른다는 W17(`:2651`)의 인과를 닫는다 |
| §3 [B-2] ↔ ADR-0004 D9 | `prev_hash` 최초값 32바이트 zero 를 유지(선택지 B)하므로 D9 를 재정의하지 않는다 |
| §3 [B-2] ↔ `ids.hpp:54` | `audit_recovery` 는 이미 `action_id` 없이 허용되는 kind 다. 새 kind 를 만들지 않으므로 헤더 변경이 없다 |
| §3 [B-5]⑥ ↔ §12-12 | 새 `process_epoch_id` → 클라이언트 전체 재조회. §12-12 "재접속" 행과 동일 규칙 |
| §3 [B-6] ↔ §6-4 | `RecoverDangling()` 이 만든 indeterminate 가 lockdown 을 켜지 않으면 §6-4 안전 통제가 재시작으로 소멸한다 |
| §3 [B-6] ↔ ADR-0004 D7 | D7 의 `operation_digest` 가 세션 수명 잠금 키이므로, **재시작 후 잠금 재수립의 키 단위도 D7 을 따라야 한다**(단, 해제 단위는 §6-4 미결) |

**모순 없음.** 다만 §3 [B-6] 은 **§6-4 의 미결(해제 단위)에 의존**한다 — 아래 미결 8번.

---

## 6. 미결 — 사람/제품 책임자 결정 필요

**임의 기본값을 넣지 않았다.** 아래는 전부 값이 비어 있는 상태로 남긴다.

| # | 미결 | 결정 주체 | 근거 |
| --- | --- | --- | --- |
| 1 | **G0-21 을 (a) ADR-0004 확장으로 닫을지 (b) ADR-0010 신설로 닫을지** | 아키텍트 | 이 문서 §A. 권고는 (b) |
| 2 | 온라인 보존 기간 / 아카이브 보존 기간 (두 값은 다르다) | 제품 책임자 (+ 법무) | `Cogito++_구현_요구사항.md:702`, `Cogito++_구현명세서.md:3032` |
| 3 | 디스크 여유 2단계 임계값, WAL 크기 2단계 임계값, 이력(hysteresis) 폭 | 제품 책임자 + 현장 | 이 문서 §2 [W-2]. **현장 볼륨 크기를 모르면 정할 수 없다** |
| 4 | 용량 감시 주기 `check_every_commits`, `wal_autocheckpoint_pages` | 아키텍트 (실측 후) | 이 문서 §2 [W-4]·[W-6]. **실측 전 수치 기재 금지**(부록 A) |
| 5 | sqlite3 포트 버전과 컴파일 옵션. 그 버전의 `wal_autocheckpoint` 기본값 | 빌드 담당 (G0-22 와 연동) | `Cogito++_구현명세서.md:2344` 의 `builtin-baseline` 이 `REPLACE_WITH_VCPKG_COMMIT_SHA` — **미확정** |
| 6 | `VACUUM INTO` 가 `AUTOINCREMENT seq` 를 보존하는지. `PRAGMA user_version` 쓰기와 `VACUUM` 의 authorizer action code | 구현 담당 (문서 확인) | 이 문서 §1 [S-4] ⚠, §3 선택지 S |
| 7 | 백업 주기·보관 위치·오프사이트 여부·암호화 방식·키 관리 | 제품 책임자 + 보안 책임자 | `Cogito++_구현_요구사항.md:702`, `:420` |
| 8 | **`operator_ack` 의 해제 단위** (operation 단위 / 라인 단위 / 전역) | 안전 책임자 | 이 문서 §3 [B-6] ⚠. **§6-4 범위이며 이 문서가 정하지 않았다** |
| 9 | 복구 후 `line_write_lockdown_` 기본 ON 정책 승인 | 안전 책임자 | 이 문서 §3 [B-6]. 안전 강화 방향이지만 운영 부담을 늘린다 |
| 10 | 마이그레이션 실행 주체와 권한. step-up 인증 필요 여부 | 제품 책임자 | `Cogito++_구현명세서.md:2806` 의 `step_up.required_for` 목록에 마이그레이션이 없다 |
| 11 | 스키마 버전 불일치 기동 실패 시의 프로세스 종료 코드 | ABI 담당 | 이 문서 §1 [S-5] |
| 12 | `audit.db` OS 계정·ACL 모델 (Windows 패널 PC / Linux 엣지 각각) | 제품 책임자 + 보안 책임자 | 이 문서 §4 [A-4], 체크리스트 `:675` |
| 13 | rollover/backup 진입점을 `AuditJournal` 인터페이스에 둘지 오프라인 CLI 로 뺄지 | 아키텍트 | 이 문서 §3 영향. 현재 인터페이스는 4개 메서드뿐(`:1136`~`:1145`) |
| 14 | 아카이브 매니페스트의 **서명 주체** | 제품 책임자 (G0-22 릴리스 서명 주체와 연동) | `Cogito++_개발_작업체크리스트.md:94` (G0-22 — "release signing 주체가 없다") |

### 이 문서가 닫지 못한 인접 결함 (별도 담당 필요)

| 결함 | 위치 | 왜 여기서 못 닫는가 |
| --- | --- | --- |
| **G0-06 이 실제로는 어느 ADR 에도 없다** | `.claude/skills/adr-draft/SKILL.md:16` vs `docs/adr/0004-…:6`. `Cogito++_구현명세서.md:1658`~`:1663` 의 `RetryFinalize()` 가 새 `Failed` payload 를 만든다 | 주제가 FSM/finalize 다. 저장소 계약이 아니다 |
| **명세에 §14·§15·§16 이 없는데 `:3025` 가 "§16에 추가" 라고 한다** | `Cogito++_구현명세서.md:3025` | 미결 항목을 모을 절 자체가 없다. 명세 구조 정정이 필요하다 |
| **§7-4 의 `PRAGMA foreign_keys = ON` 이 무의미하다** | `Cogito++_구현명세서.md:1892` — 스키마에 FOREIGN KEY 가 하나도 없다 | 무해하지만 의도가 불명확하다. 향후 FK 도입 예정이면 주석이 필요하다 |
| **`chain_anchor` 테이블이 "(선택)" 으로만 존재** | `Cogito++_구현명세서.md:1926`~`:1931`, §7-1 `anchor.enabled: false`(`:1793`) | 앵커는 §4 4층 방어의 핵심인데 기본 비활성이다. 언제 켜야 하는지 규범이 없다 |

---

## 7. 승인 요청 사항

| 결정 | 승인자 | 되돌림 | 이유 |
| --- | --- | --- | --- |
| **§A 권고 (b) — ADR-0010 신설** | 아키텍트 | 가능 | 매핑표가 지금 거짓을 말하고 있다. 어느 쪽이든 **판정 자체가 먼저 필요하다** |
| §1 [S-1]~[S-8] schema 버전·forward-only·additive-only·fail-closed | 아키텍트 | **사실상 불가** — 버전 1 의 정의가 굳으면 되돌릴 수 없다 | 마이그레이션 테스트(체크리스트 `:676`)의 대상이 이것 없이는 정의되지 않는다 |
| §2 [W-1] 감사 생략 금지의 명문화 | **안전 책임자** | 불가 | 불변식 4·8 의 재확인. **이것만은 값 미결과 무관하게 즉시 확정 가능하다** |
| §2 [W-2]~[W-6] 임계 상태기 (**구조만**, 값은 미결) | 아키텍트 + 제품 책임자 | 가능 | 값이 정해지기 전에도 구조는 고정할 수 있다 |
| §3 [B-1]~[B-5] 아카이브·백업·복구 절차 | 아키텍트 + 제품 책임자 | 가능 | 체크리스트 `:1721` 이 ADR 을 명시적으로 요구 |
| **§3 [B-6] 복구 후 lockdown ON** | **안전 책임자** | 가능 | **현재 프로세스 재시작이 §6-4 안전 통제를 조용히 해제한다.** 이 결함은 값 미결과 무관하다 |
| §4 [A-1]~[A-4] append-only 4층 서술 | 아키텍트 + 보안 책임자 | 가능 | 표기 정확성. `CLAUDE.md:110` 의 금지 표현을 한 곳에 정확히 모은다 |

**값 없이도 지금 확정할 수 있는 것 3개**: §2 [W-1], §3 [B-6], §4 [A-1]~[A-3].
셋 다 **안전 불변식의 직접 귀결**이며 현장값 결정을 기다릴 이유가 없다.

**나머지는 현장값이 없으면 확정할 수 없다.** 이 문서는 그 값들을 지어내지 않았다.
