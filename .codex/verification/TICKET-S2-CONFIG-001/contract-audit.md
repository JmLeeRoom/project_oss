# TICKET-S2-CONFIG-001 Contract Preflight

- Recorded: 2026-08-25
- Verdict: `NEEDS_GEMINI`
- Product writes: `0`
- Branch activation: not performed
- Stage Exit Gate: `NOT_EVALUATED`

Two independent read-only reviews found that implementation cannot start from the
current protected contract.

## Blocking findings

1. **Limited-start authorization is not aligned.**
   `docs/prompts/TASK_PROMPT_CODEX.md` claims Config-specific authorization, but
   `docs/g0/G0-LEDGER.md` limits the approved S2 scope to Action, Tool Schema,
   Tool Contract, Registry, Policy, Clock, and SecretString. The prompt is lower
   authority than the ledger.

2. **The required Config projection conflicts with Accepted G0-26.**
   `docs/g0/G0-RESOLUTION-9.md` and `include/cogito/digest.hpp` exclude absolute
   paths from `config_digest`. The prompt requires each `SecretRef.uri` verbatim,
   and Golden Vector 2 includes `file:/etc/secrets/db.pass`. All three published
   SHA-256 values independently recompute correctly for their stated bytes, but
   Vector 2's bytes violate the approved projection. Vector 1 is a direct
   `ComputeConfigDigest` fixture and cannot be produced by
   `CogitoConfig::ToNormalizedJson()`.

3. **Secret resolution is not a complete security contract.**
   The URI grammar accepts empty or malformed locations; lowercase normalization
   is incompatible with a raw `string_view` return unless inputs are already
   constrained; POSIX mode and owner rules conflict; Windows SID/ACE rules are
   inconsistent; check-then-open leaves a TOCTOU gap; file encoding, NUL, newline,
   empty-file, growth, and exact 65,536-byte behavior are unspecified; and
   wincred/keyring lack a platform/API/encoding/size/test-seam contract.

4. **Strict JSON and schema behavior conflict.**
   The checklist requires Strict JSON and duplicate rejection, while the prompt
   remaps all parsing failures to `ConfigError` instead of defining preservation
   of `DuplicateKey`, `NotUtf8`, `DepthExceeded`, and `TooLarge`. The 1 MiB load
   cap is not reconciled with `ParseStrict`'s 256 KiB default. The config schema's
   pattern lacks the Accepted G0-10 terminal anchor and `maxLength`; it contains
   constructs rejected by the existing `SchemaCompiler`; and it lacks bounds for
   values narrowed into public integer fields.

5. **Ticket traceability overclaims S2 completion.**
   Checklist S2-07 is Registry/Policy projection digest, not Config. Full S2-09
   includes startup pipeline and fail-before-ready behavior outside the six-file
   allowlist, so this ticket cannot close the S2 Exit Gate.

## Minimum protected-contract corrections

1. Record the human-approved Config limited-start scope in the G0 ledger.
2. Define a path-free `file:` reference projection, align G0-26/ADR/header/prompt,
   regenerate Vector 2, and label Vector 1 as a direct digest-function fixture.
3. Publish per-scheme URI and content limits, supported backend matrix and test
   seam, exact POSIX owner/mode and Windows SID/ACE policy, and same-handle
   race-safe validation/read semantics with complete `Errc`/`reason_code` mapping.
4. Define effective `ParseStrict` limits and error mapping; make
   `config/cogito.schema.json` compliant with G0-10 and public numeric/string/count
   bounds; specify whether validation consumes that file or an embedded resource.
5. Correct S2-07/S2-09 traceability and narrow the acceptance claim to this
   ticket's actual scope.

No files in `src/**`, `tests/**`, `include/**`, `docs/**`, or `config/**` were
changed by this preflight.
