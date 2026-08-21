from __future__ import annotations

import importlib.util
import json
import re
import subprocess
import sys
import tempfile
import time
import tomllib
import unittest
from pathlib import Path
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parents[1] / "cogito_hooks.py"
REPO_ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("cogito_hooks", MODULE_PATH)
assert SPEC and SPEC.loader
HOOKS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HOOKS)

TICKET = "S1-05"
BASELINE_OID = "a" * 40
EVIDENCE_REL = "artifacts/verification/S1/abc"
MANIFEST_REL = EVIDENCE_REL + "/manifest.json"
VERDICT_REL = EVIDENCE_REL + "/verdict.json"
VERIFY_COMMAND = "ctest --preset windows-msvc-debug -C Debug --output-on-failure"


def active_state() -> dict:
    state = HOOKS.default_state()
    state.update(
        {
            "active_ticket": "S1-05",
            "baseline_head_oid": BASELINE_OID,
            "phase": "implementation",
            "g0_gate": "passed",
            "contract_review": "pass",
            "contract_reference": "approved-contract-sha256:abc",
            "allowed_write_paths": ["src/canonical_json.cpp", "tests/canonical/"],
        }
    )
    state["verification"].update(
        {
            "ticket": TICKET,
            "commands": [VERIFY_COMMAND],
            "evidence_dir": EVIDENCE_REL,
            "manifest_file": MANIFEST_REL,
            "verdict_file": VERDICT_REL,
        }
    )
    return state


def verified_complete_state(*, stage_exit: bool = False) -> dict:
    state = active_state()
    state["phase"] = "complete"
    state["verification"]["verifier"] = "pass"
    state["verification"]["stage_exit_gate"] = "pass" if stage_exit else "not_evaluated"
    return state


def completed_state_with_evidence(
    root: Path, *, extra_shared_paths: tuple[str, ...] = ()
) -> dict:
    state = active_state()
    state["phase"] = "complete"
    evidence_rel = EVIDENCE_REL
    state["allowed_write_paths"].append(evidence_rel + "/")
    state["allowed_write_paths"].extend(extra_shared_paths)
    state["shared_write"] = {
        "approved": True,
        "paths": [evidence_rel + "/", *extra_shared_paths],
        "reference": "user approved checklist section 18 evidence path",
    }
    identity = HOOKS.scope_identity(root, state)
    evidence = root / evidence_rel
    evidence.mkdir(parents=True)
    output_rel = evidence_rel + "/command-01.log"
    (root / output_rel).write_text("1/1 tests passed\n", encoding="utf-8")
    manifest_rel = MANIFEST_REL
    verdict_rel = VERDICT_REL
    command = VERIFY_COMMAND
    manifest = {
        "schema_version": 1,
        "ticket": "S1-05",
        **identity,
        "toolchain": {"python": "3.14.3", "cmake": "4.x"},
        "commands": [{"command": command, "exit_code": 0, "output_file": output_rel}],
        "acceptance": [{"id": "S1-05-A1", "status": "PASS", "evidence": [output_rel]}],
        "evidence_files": {output_rel: HOOKS._sha256_file(root / output_rel)},
        "redaction": {"status": "PASS"},
    }
    (root / manifest_rel).write_text(json.dumps(manifest), encoding="utf-8")
    verdict = {
        "schema_version": 1,
        "ticket": "S1-05",
        "verdict": "PASS",
        "stage_exit_gate": "not_evaluated",
        "manifest_sha256": HOOKS._sha256_file(root / manifest_rel),
        **identity,
        "acceptance": [{"id": "S1-05-A1", "status": "PASS"}],
    }
    (root / verdict_rel).write_text(json.dumps(verdict), encoding="utf-8")
    state["verification"] = {
        "ticket": "S1-05",
        "commands": [command],
        "evidence_dir": evidence_rel,
        "manifest_file": manifest_rel,
        "verdict_file": verdict_rel,
        "verifier": "pass",
        "stage_exit_gate": "not_evaluated",
        **identity,
    }
    return state


class StateValidationTests(unittest.TestCase):
    def test_runtime_enforces_schema_identity_required_and_unknown_keys(self) -> None:
        cases = []
        wrong_schema = HOOKS.default_state()
        wrong_schema["$schema"] = "wrong"
        cases.append(wrong_schema)
        unknown_root = HOOKS.default_state()
        unknown_root["unexpected"] = True
        cases.append(unknown_root)
        unknown_nested = HOOKS.default_state()
        unknown_nested["publication"]["unexpected"] = True
        cases.append(unknown_nested)
        missing = HOOKS.default_state()
        del missing["safety"]
        cases.append(missing)
        empty_nullable_path = HOOKS.default_state()
        empty_nullable_path["verification"]["evidence_dir"] = ""
        cases.append(empty_nullable_path)
        empty_head = HOOKS.default_state()
        empty_head["verification"]["head_oid"] = ""
        cases.append(empty_head)
        windows_duplicate = HOOKS.default_state()
        windows_duplicate["allowed_write_paths"] = ["src/A.cpp", "SRC/a.cpp"]
        cases.append(windows_duplicate)
        for state in cases:
            with self.subTest(state=state):
                self.assertTrue(HOOKS.validate_state(state))

    def test_unhashable_state_values_fail_as_validation_errors(self) -> None:
        for field_path in (
            ("phase",),
            ("g0_gate",),
            ("contract_review",),
            ("verification", "verifier"),
            ("verification", "stage_exit_gate"),
        ):
            state = HOOKS.default_state()
            target = state
            for field in field_path[:-1]:
                target = target[field]
            target[field_path[-1]] = []
            with self.subTest(field_path=field_path):
                self.assertTrue(HOOKS.validate_state(state))

    def test_load_state_converts_invalid_utf8_and_validation_defects(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            state_path = root / ".codex" / "stage-state.json"
            state_path.parent.mkdir()
            state_path.write_bytes(b"\xff")
            with self.assertRaises(HOOKS.StateError):
                HOOKS.load_state(root)
            malformed = HOOKS.default_state()
            malformed["phase"] = []
            state_path.write_text(json.dumps(malformed), encoding="utf-8")
            with self.assertRaises(HOOKS.StateError):
                HOOKS.load_state(root)

    def test_ownership_loader_fails_closed_for_real_corrupt_data(self) -> None:
        valid_policy = json.loads(HOOKS.OWNERSHIP_POLICY_PATH.read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "ownership-policy.json"
            payloads = [b"[]", b"\xff"]
            invalid_owner = json.loads(json.dumps(valid_policy))
            invalid_owner["rules"][0]["owner"] = []
            payloads.append(json.dumps(invalid_owner).encode("utf-8"))
            for payload in payloads:
                path.write_bytes(payload)
                with self.subTest(payload=payload[:20]), mock.patch.object(
                    HOOKS, "OWNERSHIP_POLICY_PATH", path
                ):
                    rules, fallback, prefixes, error = HOOKS._load_ownership_policy_fail_closed()
                    self.assertEqual(rules, [])
                    self.assertEqual(fallback, "shared")
                    self.assertEqual(prefixes, ())
                    self.assertIsInstance(error, str)


class PatchGuardTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path.cwd()

    def decision(self, path: str, state: dict | None = None, branch: str = "codex/s1-05-ccj") -> dict:
        patch = f"*** Begin Patch\n*** Update File: {path}\n@@\n-old\n+new\n*** End Patch"
        return HOOKS.apply_patch_decision(patch, self.root, state or active_state(), branch)

    def test_extracts_explicit_patch_paths(self) -> None:
        patch = "*** Add File: src/a.cpp\n*** Update File: tests/core/a.cpp"
        self.assertEqual(HOOKS.extract_patch_paths(patch), ["src/a.cpp", "tests/core/a.cpp"])

    def test_allows_only_preflight_state_patch_on_main(self) -> None:
        self.assertEqual(self.decision(".codex/stage-state.json", branch="main"), {})
        result = self.decision("src/canonical_json.cpp", branch="main")
        self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_blocks_path_traversal(self) -> None:
        result = self.decision("../src/canonical_json.cpp")
        self.assertIn("parent traversal", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_delete_without_explicit_authorization(self) -> None:
        patch = "*** Begin Patch\n*** Delete File: src/canonical_json.cpp\n*** End Patch"
        result = HOOKS.apply_patch_decision(patch, self.root, active_state(), "codex/s1-05-ccj")
        self.assertIn("deletion", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_move_without_explicit_authorization(self) -> None:
        patch = (
            "*** Begin Patch\n*** Update File: src/canonical_json.cpp\n"
            "*** Move to: tests/canonical/moved.cpp\n@@\n-old\n+new\n*** End Patch"
        )
        result = HOOKS.apply_patch_decision(patch, self.root, active_state(), "codex/s1-05-ccj")
        self.assertIn("deletion or move", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_windows_path_aliases(self) -> None:
        for path in (".codex./hooks.json", "CLAUDE.md::$DATA", "src/NUL.txt"):
            with self.subTest(path=path):
                result = self.decision(path)
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_blocks_canonical_target_alias(self) -> None:
        target = self.root.resolve(strict=False) / "include" / "cogito" / "result.hpp"
        with mock.patch.object(HOOKS, "_safe_resolve", return_value=target):
            result = self.decision("src/canonical_json.cpp")
        self.assertIn("filesystem alias", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_claude_owned_header(self) -> None:
        result = self.decision("include/cogito/result.hpp")
        self.assertIn("Claude-owned", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_claude_root_instructions(self) -> None:
        result = self.decision("CLAUDE.md")
        self.assertIn("Claude-owned", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_antigravity_web_file(self) -> None:
        result = self.decision("tools/web_dashboard/src/App.tsx")
        self.assertIn("Antigravity-owned", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_product_write_while_idle(self) -> None:
        result = self.decision("src/canonical_json.cpp", HOOKS.default_state())
        self.assertIn("Select exactly one", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_product_write_outside_allowlist(self) -> None:
        result = self.decision("src/digest.cpp")
        self.assertIn("outside", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_product_write_on_wrong_ticket_branch(self) -> None:
        result = self.decision("src/canonical_json.cpp", branch="codex/s1-06-digest")
        self.assertIn("codex/s1-05-<slug>", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_allows_exact_active_ticket_path(self) -> None:
        self.assertEqual(self.decision("src/canonical_json.cpp"), {})

    def test_allows_codex_meta_on_feature_branch(self) -> None:
        self.assertEqual(self.decision(".codex/README.md"), {})

    def test_allows_codex_skill_exception_but_blocks_other_agents_assets(self) -> None:
        self.assertEqual(self.decision(".agents/skills/cogito-stage-owner/SKILL.md"), {})
        result = self.decision(".agents/skills/mock-api-engine/SKILL.md")
        self.assertIn("Antigravity-owned", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_shared_path_without_exact_human_approval(self) -> None:
        result = self.decision("scripts/ownership-policy.json")
        self.assertIn("shared scope", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_exact_file_allowlist_does_not_expand_to_descendants(self) -> None:
        self.assertFalse(HOOKS.path_is_within("src/new.cpp/extra.cpp", "src/new.cpp"))
        self.assertTrue(HOOKS.path_is_within("tests/canonical/extra.cpp", "tests/canonical/"))
        result = self.decision("src/canonical_json.cpp/extra.cpp")
        self.assertIn("outside", result["hookSpecificOutput"]["permissionDecisionReason"])


class ShellGuardTests(unittest.TestCase):
    def test_blocks_destructive_git(self) -> None:
        for command in (
            "git reset --hard HEAD~1",
            "env git reset --hard HEAD~1",
            "if true; then git reset --hard HEAD~1; fi",
            "& C:\\Git\\bin\\git.exe reset --hard HEAD~1",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, active_state(), "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_blocks_shell_file_write(self) -> None:
        result = HOOKS.bash_decision("Set-Content -Path src/a.cpp -Value x", active_state(), "codex/s1-05-ccj")
        self.assertIn("apply_patch", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_shell_mutation_bypass_families(self) -> None:
        commands = (
            "echo hacked > include/cogito/result.hpp",
            "cmd /c del include\\cogito\\result.hpp",
            'py -3 -c "from pathlib import Path; Path(\'include/cogito/result.hpp\').unlink()"',
            "xcopy /Y src\\x.hpp include\\x.hpp",
            "robocopy src include x.hpp",
            "powershell -File mutate.ps1",
            "py mutate.py",
            "sed -i s/a/b/ src/canonical_json.cpp",
            "rsync src/a include/a",
            "Expand-Archive p.zip -DestinationPath include",
            "sc include\\x.hpp hacked",
            "git rm src/canonical_json.cpp",
            "[IO.File]::WriteAllText('CLAUDE.md','x')",
        )
        for command in commands:
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, active_state(), "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_blocks_unplanned_runner_while_idle(self) -> None:
        result = HOOKS.bash_decision("py mutate.py", HOOKS.default_state(), "codex/ops-bootstrap")
        self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_control_helper_read_modes_remain_available_while_idle(self) -> None:
        for option in ("--validate-state", "--fingerprint"):
            command = f"py -3 -B .codex/hooks/cogito_hooks.py {option}"
            with self.subTest(option=option):
                self.assertEqual(
                    HOOKS.bash_decision(command, HOOKS.default_state(), "codex/ops-bootstrap"),
                    {},
                )
        for command in (
            "py -3 -B src/.codex/hooks/cogito_hooks.py --validate-state",
            "python C:\\tmp\\.codex\\hooks\\cogito_hooks.py --fingerprint",
            "py -c .codex/hooks/cogito_hooks.py --validate-state",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(
                    command, HOOKS.default_state(), "codex/ops-bootstrap"
                )
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_default_deny_closes_unrecognized_shell_and_git_mutators(self) -> None:
        commands = (
            "printf hacked | sponge include/cogito/result.hpp",
            "clang-format -i include/cogito/result.hpp",
            "git archive --output=include/out.zip HEAD",
            "git config --file include/policy.ini x.y z",
            "git branch --unset-upstream",
            "git branch --edit-description",
            "git branch -C codex/s1-05-target",
            "git branch unrelated",
            "git branch -df unrelated",
            "git branch --track unrelated origin/main",
            "git -c diff.external=opaque-writer diff",
            "git grep --open-files-in-pager=opaque-writer needle",
            "git grep -Oopaque-writer needle",
            "git grep -O opaque-writer needle",
            "git cat-file --filters HEAD:path",
            "git cat-file --filters=HEAD:path",
            "git cat-file --textconv=HEAD:path",
            "git show --textconv HEAD:path",
            "git -p log",
            "git --paginate diff",
            "git remote -v add trap ext::opaque-writer",
            "git remote --verbose set-url origin ext::opaque-writer",
            "git branch --color=always unrelated",
            "git branch --ignore-case unrelated",
            "git diff --outpu=include/out.txt",
            "git diff --ext-dif",
            "git show --textcon HEAD:path",
            "git grep --open-files-in-page=opaque-writer needle",
            "tree --outpu=include/out.txt .",
            "env GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=diff.external GIT_CONFIG_VALUE_0=opaque-writer git diff",
            "env GIT_EXTERNAL_DIFF=opaque-writer git diff",
            "env GIT_PAGER=opaque-writer git log",
            "g''it reset --hard HEAD~1",
            "time git reset --hard HEAD~1",
            'g=git; "$g" reset --hard HEAD~1',
            "tree -o include/out.txt .",
        )
        for command in commands:
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, active_state(), "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_simple_read_only_shell_and_git_inspection_remain_available(self) -> None:
        for command in (
            "rg -n TODO src",
            "Get-Content -Raw README.md",
            "git --no-pager status --short",
            "git --no-pager diff --no-ext-diff --no-textconv --check",
            "git --no-pager log --no-ext-diff --no-textconv -1",
            "git --no-pager remote",
            "git --no-pager remote -v",
            "git --no-pager remote get-url origin",
            "git --no-pager config --get core.repositoryformatversion",
        ):
            with self.subTest(command=command):
                self.assertEqual(
                    HOOKS.bash_decision(command, active_state(), "codex/s1-05-ccj"),
                    {},
                )

    def test_diff_show_and_log_require_internal_projection_flags(self) -> None:
        for command in (
            "git diff --check",
            "git --no-pager diff --check",
            "git --no-pager log --no-ext-diff -1",
            "git --no-pager show --no-textconv HEAD",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, active_state(), "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_diff_safeguards_cannot_be_consumed_as_option_values(self) -> None:
        for subcommand in ("diff", "show", "log"):
            command = (
                f"git --no-pager {subcommand} --src-prefix --no-ext-diff "
                "--dst-prefix --no-textconv"
            )
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, active_state(), "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_git_shell_lexical_concatenation_and_expansion_are_blocked(self) -> None:
        commands = (
            "git --no-pager diff --no-ext-diff --no-textconv --out`put=owned.txt",
            'git --no-pager diff --no-ext-diff --no-textconv --out"put=owned.txt"',
            "git --no-pager diff --no-ext-diff --no-textconv --out'put=owned.txt'",
            r"git --no-pager diff --no-ext-diff --no-textconv --out\put=owned.txt",
            "git --no-pager diff --no-ext-diff --no-textconv --out^put=owned.txt",
            "git --no-pager diff --no-ext-diff --no-textconv --out%PATH%put=owned.txt",
        )
        for command in commands:
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, active_state(), "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_git_glob_expansion_is_blocked_for_every_allowed_shape(self) -> None:
        active = active_state()
        active["publication"].update(
            {
                "commit_authorized": True,
                "merge_authorized": True,
                "reference": "user request turn 42",
            }
        )
        complete = verified_complete_state(stage_exit=True)
        complete["publication"].update(
            {"release_authorized": True, "reference": "user request turn 42"}
        )
        cases = (
            ("git --no-pager diff --no-ext-diff --no-textconv *", active),
            ("git --no-pager diff --no-ext-diff --no-textconv @args", active),
            ("git --no-pager config --get user.?ame", active),
            ("git --no-pager config --get @args", active),
            ("git commit --no-gpg-sign --no-verify -m *", active),
            ("git commit --no-gpg-sign --no-verify -m @args", active),
            ("git merge --no-edit --no-verify --no-gpg-sign [a]", active),
            ("git merge --no-edit --no-verify --no-gpg-sign @args", active),
            ("git tag --no-sign *", complete),
            ("git tag --no-sign @args", complete),
        )
        for command, state in cases:
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, state, "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_config_queries_use_exact_read_only_shapes(self) -> None:
        for command in (
            "git --no-pager config --file --get user.name hacked",
            "git --no-pager config --file --list user.name hacked",
            "git --no-pager config --get user.name extra",
            "git --no-pager config --list extra",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, active_state(), "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_allows_only_exact_active_ticket_planned_runner(self) -> None:
        state = active_state()
        command = "py -3 scripts/approved-verifier.py --read-only"
        self.assertIn(
            "must exactly match",
            HOOKS.bash_decision(command, state, "codex/s1-05-ccj")[
                "hookSpecificOutput"
            ]["permissionDecisionReason"],
        )
        state["verification"]["commands"].append(command)
        self.assertEqual(HOOKS.bash_decision(command, state, "codex/s1-05-ccj"), {})

    def test_planned_runner_is_blocked_outside_active_ticket_execution(self) -> None:
        command = "python scripts/mutator.py"
        state = active_state()
        state["verification"]["commands"].append(command)
        cases = (
            ("preflight", "main"),
            ("implementation", "main"),
            ("implementation", "codex/s1-06-other"),
            ("complete", "codex/s1-05-ccj"),
        )
        for phase, branch in cases:
            with self.subTest(phase=phase, branch=branch):
                state["phase"] = phase
                result = HOOKS.bash_decision(command, state, branch)
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")
        state["phase"] = "verification"
        self.assertEqual(HOOKS.bash_decision(command, state, "codex/s1-05-ccj"), {})

    def test_ticket_branch_creation_is_exact_and_branch_queries_stay_read_only(self) -> None:
        state = active_state()
        for command in (
            "git branch codex/s1-05-target",
            "git switch -c codex/s1-05-target",
            "git checkout -b codex/s1-05-target",
            "git --no-pager branch",
            "git --no-pager branch --list unrelated",
            "git --no-pager branch --list --sort=refname unrelated",
        ):
            with self.subTest(command=command):
                self.assertEqual(
                    HOOKS.bash_decision(command, state, "codex/s1-05-ccj"),
                    {},
                )

    def test_blocks_commit_without_user_authorization(self) -> None:
        result = HOOKS.bash_decision("git commit -m test", active_state(), "codex/s1-05-ccj")
        self.assertIn("not authorized", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_commit_with_global_git_options(self) -> None:
        for command in (
            'git -C "C:/repo" --no-pager commit -m test',
            "git --git-dir=.git commit -m test",
            "if ($true) { git commit -m test }",
            "git -c alias.ship=commit ship -m test",
            "sudo git commit --no-gpg-sign --no-verify -m test",
            "command git commit --no-gpg-sign --no-verify -m test",
            "& git commit --no-gpg-sign --no-verify -m test",
            "C:/Git/bin/git.exe commit --no-gpg-sign --no-verify -m test",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, active_state(), "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_blocks_command_scoped_git_aliases_before_alias_expansion(self) -> None:
        result = HOOKS.bash_decision(
            "git -c alias.hide='!opaque-tool payload' hide",
            active_state(),
            "codex/s1-05-ccj",
        )
        self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_github_cli_publication_is_disabled_in_favor_of_connector(self) -> None:
        state = verified_complete_state(stage_exit=True)
        state["publication"]["release_authorized"] = True
        state["publication"]["pull_request_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        commands = (
            'opaque-writer "gh release create v1"',
            "opaque-writer; gh release create v1",
            'opaque-writer "gh pr create"',
            "gh release create v1",
            "gh pr create --draft",
            "gh pr merge",
            '"gh" pr create',
            "command gh pr create",
            "C:/tools/gh.exe pr create",
            r"C:\tools\gh.exe pr create",
            'g"h" pr create',
            r"g\h pr create",
            "g`h pr create",
        )
        state["verification"]["commands"].extend(commands)
        for command in commands:
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, state, "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")
    def test_allows_commit_only_with_explicit_reference(self) -> None:
        state = active_state()
        state["publication"]["commit_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        self.assertEqual(
            HOOKS.bash_decision(
                "git commit --no-gpg-sign --no-verify -m test",
                state,
                "codex/s1-05-ccj",
            ),
            {},
        )

    def test_authorized_commit_does_not_imply_amend_authority(self) -> None:
        state = active_state()
        state["publication"]["commit_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        result = HOOKS.bash_decision(
            "git commit --amend --no-edit", state, "codex/s1-05-ccj"
        )
        self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_authorized_commit_is_noninteractive_unsigned_and_hook_bypassed(self) -> None:
        state = active_state()
        state["publication"]["commit_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        for command in (
            "git commit -m test",
            "git commit --no-gpg-sign --no-verify --edit -m test",
            "git commit --no-gpg-sign --no-verify --edi -m test",
            "git commit --no-gpg-sign --no-verify -e -m test",
            "git commit --no-verify -Skey -m test",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, state, "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_commit_safeguards_must_be_real_options_in_exact_shape(self) -> None:
        state = active_state()
        state["publication"]["commit_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        for command in (
            "git commit -m --no-gpg-sign -m --no-verify -m test",
            "git commit --message --no-gpg-sign --message --no-verify --message test",
            "git commit --no-gpg-sign --no-verify --verify -m test",
            "git commit --no-gpg-sign --no-verify -m test docs/contract.md",
            "git commit -a --no-gpg-sign --no-verify -m test",
            'git commit --no-gpg-sign --no-verify --gpg-"sign" -m test',
            "git commit --no-gpg-sign --no-verify --am`end -m test",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, state, "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_commit_requires_active_ticket_phase_and_exact_branch(self) -> None:
        state = active_state()
        state["publication"]["commit_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        command = "git commit --no-gpg-sign --no-verify -m test"
        for phase, branch in (
            ("preflight", "codex/s1-05-ccj"),
            ("blocked", "codex/s1-05-ccj"),
            ("implementation", "main"),
            ("implementation", "master"),
            ("implementation", None),
            ("implementation", "feature/foo"),
            ("implementation", "codex/s2-01-other"),
        ):
            state["phase"] = phase
            with self.subTest(phase=phase, branch=branch):
                result = HOOKS.bash_decision(command, state, branch)
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_staged_commit_scope_rejects_foreign_and_outside_paths(self) -> None:
        state = active_state()
        for paths in (
            ["include/cogito/result.hpp"],
            ["src/digest.cpp"],
            [".codex/stage-state.json"],
        ):
            with self.subTest(paths=paths), mock.patch.object(
                HOOKS, "_staged_paths", return_value=paths
            ):
                result = HOOKS._staged_commit_decision(
                    Path.cwd(), state, "codex/s1-05-ccj"
                )
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")
        with mock.patch.object(
            HOOKS, "_staged_paths", return_value=["src/canonical_json.cpp"]
        ):
            self.assertEqual(
                HOOKS._staged_commit_decision(Path.cwd(), state, "codex/s1-05-ccj"),
                {},
            )

    def test_release_authority_does_not_grant_sign_force_or_delete(self) -> None:
        state = verified_complete_state(stage_exit=True)
        state["publication"]["release_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        for command in (
            "git tag v1",
            "git tag -s v1 -m signed",
            "git tag -u key v1",
            "git tag -f v1 HEAD~1",
            "git tag -d v1",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, state, "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")
        self.assertEqual(
            HOOKS.bash_decision("git tag --no-sign v1", state, "codex/s1-05-ccj"),
            {},
        )

    def test_tag_is_lightweight_literal_and_stage_verified(self) -> None:
        state = verified_complete_state(stage_exit=True)
        state["publication"]["release_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        for command in (
            "git tag -m --no-sign v1",
            "git tag --no-sign -a v1",
            "git tag --no-sign --annotate v1",
            "git tag --no-sign --force v1",
            "git tag --no-sign v1 other-branch",
            'git tag --no-sign --s"ign v1',
            "git tag --no-sign --fo`rce v1",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, state, "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")
        for branch in ("main", "master", None, "feature/foo"):
            with self.subTest(branch=branch):
                result = HOOKS.bash_decision("git tag --no-sign v1", state, branch)
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")
        state["verification"]["stage_exit_gate"] = "not_evaluated"
        result = HOOKS.bash_decision("git tag --no-sign v1", state, "codex/s1-05-ccj")
        self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_integration_authority_blocks_external_helpers_and_requires_safe_flags(self) -> None:
        state = active_state()
        state["publication"]["merge_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        for command in (
            "git rebase --exec opaque-writer main",
            "git rebase -x opaque-writer main",
            "git merge --strategy=opaque-writer main",
            "git merge -s opaque-writer main",
            "git pull --upload-pack=opaque-writer origin main",
            "git merge main",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, state, "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")
        for command in (
            "git merge --no-edit --no-verify --no-gpg-sign main",
            "git rebase --no-verify --no-gpg-sign main",
            "git pull --no-edit --no-verify --no-gpg-sign origin main",
        ):
            with self.subTest(command=command):
                self.assertEqual(
                    HOOKS.bash_decision(command, state, "codex/s1-05-ccj"),
                    {},
                )

    def test_integration_safeguards_are_structural_and_remote_is_named(self) -> None:
        state = active_state()
        state["publication"]["merge_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        for command in (
            "git merge -m --no-edit -m --no-verify -m --no-gpg-sign main",
            "git merge --no-edit --no-verify --no-gpg-sign --verify main",
            "git pull -m --no-edit -m --no-verify -m --no-gpg-sign origin main",
            "git pull --no-edit --no-verify --no-gpg-sign ext::helper main",
            "git pull --no-edit --no-verify --no-gpg-sign https://example.invalid/repo main",
            "git rebase --no-verify --no-gpg-sign --verify main",
            'git merge --no-edit --no-verify --no-gpg-sign --gpg-"sign" main',
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, state, "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_all_integrations_require_ticket_branch_and_execution_phase(self) -> None:
        state = active_state()
        state["publication"]["merge_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        commands = (
            "git merge --no-edit --no-verify --no-gpg-sign main",
            "git rebase --no-verify --no-gpg-sign main",
            "git cherry-pick --no-gpg-sign abc",
            "git pull --no-edit --no-verify --no-gpg-sign origin main",
            "git revert --no-edit --no-gpg-sign abc",
        )
        for command in commands:
            for branch in ("main", "master", None, "feature/foo", "codex/s2-01-other"):
                with self.subTest(command=command, branch=branch):
                    result = HOOKS.bash_decision(command, state, branch)
                    self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")
        for phase in ("idle", "preflight", "blocked", "complete"):
            state["phase"] = phase
            with self.subTest(phase=phase):
                result = HOOKS.bash_decision(commands[0], state, "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_blocks_push_to_main_even_when_authorized(self) -> None:
        state = verified_complete_state()
        state["publication"]["push_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        for command in (
            "git push origin main",
            "git push origin HEAD:refs/heads/main",
            "git push origin :main",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, state, "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_blocks_normal_feature_push_without_authorization(self) -> None:
        state = verified_complete_state()
        result = HOOKS.bash_decision(
            "git push origin codex/s1-05-ccj", state, "codex/s1-05-ccj"
        )
        self.assertIn("not authorized", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_force_push_even_when_authorized(self) -> None:
        state = verified_complete_state()
        state["publication"]["push_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        for command in (
            "git push --force-with-lease origin codex/s1-05-ccj",
            "git push --force-with-lease=refs/heads/feature origin feature",
            "git push origin +HEAD:refs/heads/feature",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, state, "codex/s1-05-ccj")
                self.assertIn("Force-push", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_git_ref_plumbing(self) -> None:
        for command in (
            "git update-ref refs/heads/main HEAD",
            "& 'git.exe' update-ref refs/heads/main HEAD",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, active_state(), "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_blocks_destructive_ref_and_history_bypasses(self) -> None:
        for command in (
            "git branch -f main HEAD",
            "git switch -C main",
            "git reset --soft HEAD~1",
            "git revert HEAD",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, active_state(), "codex/s1-05-ccj")
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_blocks_implicit_protected_and_bulk_push_bypasses(self) -> None:
        state = active_state()
        state["publication"]["push_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        cases = (
            ("git push --mirror origin", "codex/s1-05-ccj"),
            ("git push origin", "main"),
            ("git push origin", None),
            ("git -c push.default=matching push origin", "codex/s1-05-ccj"),
            (
                "git -c remote.origin.push=HEAD:refs/heads/main push origin",
                "codex/s1-05-ccj",
            ),
            ("git push -fu origin feature", "codex/s1-05-ccj"),
        )
        for command, branch in cases:
            with self.subTest(command=command, branch=branch):
                result = HOOKS.bash_decision(command, state, branch)
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_authorized_push_requires_current_branch_single_refspec(self) -> None:
        state = verified_complete_state()
        state["publication"]["push_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        branch = "codex/s1-05-ccj"
        for command in (
            "git push origin codex/s1-05-ccj",
            "git push -u origin HEAD:refs/heads/codex/s1-05-ccj",
        ):
            with self.subTest(command=command):
                self.assertEqual(HOOKS.bash_decision(command, state, branch), {})
        result = HOOKS.bash_decision("git push origin feature:other", state, branch)
        self.assertIn(
            "current reviewed ticket branch",
            result["hookSpecificOutput"]["permissionDecisionReason"],
        )
        result = HOOKS.bash_decision(f"git push origin :{branch}", state, branch)
        self.assertIn("empty source", result["hookSpecificOutput"]["permissionDecisionReason"])
        for command in (
            "git push origin evil:codex/s1-05-ccj",
            "git push --receive-pack=opaque-writer origin codex/s1-05-ccj",
            "git push ext::opaque-writer codex/s1-05-ccj",
            "git push origin codex/s1-05-ccj:refs/heads/Codex/S1-05-CCJ",
        ):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, state, branch)
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_push_requires_verified_completion_and_exact_ticket_branch(self) -> None:
        state = verified_complete_state()
        state["publication"]["push_authorized"] = True
        state["publication"]["reference"] = "user request turn 42"
        for branch in ("main", "master", None, "feature/foo", "codex/s2-01-other"):
            ref = branch or "codex/s1-05-ccj"
            with self.subTest(branch=branch):
                result = HOOKS.bash_decision(f"git push origin {ref}", state, branch)
                self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")
        state["phase"] = "preflight"
        result = HOOKS.bash_decision(
            "git push origin codex/s1-05-ccj", state, "codex/s1-05-ccj"
        )
        self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_blocks_merge_and_pull_without_authorization(self) -> None:
        for command in ("git merge main", "git pull origin main", "git rebase main", "git cherry-pick abc"):
            with self.subTest(command=command):
                result = HOOKS.bash_decision(command, active_state(), "codex/s1-05-ccj")
                self.assertIn("explicit user authorization", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_blocks_opaque_shell_patch(self) -> None:
        result = HOOKS.bash_decision("git apply change.patch", active_state(), "codex/s1-05-ccj")
        self.assertIn("apply_patch", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_shell_command_alias_uses_same_guard(self) -> None:
        payload = {
            "tool_name": "shell_command",
            "tool_input": {"command": "git reset --hard HEAD~1"},
        }
        result = HOOKS.pre_tool_decision(payload, Path.cwd(), active_state(), "codex/s1-05-ccj")
        self.assertIn("destructive", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_direct_write_tool_checks_owner(self) -> None:
        payload = {"tool_name": "Write", "tool_input": {"file_path": "CLAUDE.md"}}
        result = HOOKS.pre_tool_decision(payload, Path.cwd(), active_state(), "codex/s1-05-ccj")
        self.assertIn("Claude-owned", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_subagent_identity_cannot_use_write_capable_tool(self) -> None:
        payload = {
            "agent_id": "subagent-1",
            "agent_type": "contract_auditor",
            "tool_name": "apply_patch",
            "tool_input": {
                "command": "*** Begin Patch\n*** Update File: src/canonical_json.cpp\n*** End Patch"
            },
        }
        result = HOOKS.pre_tool_decision(payload, Path.cwd(), active_state(), "codex/s1-05-ccj")
        self.assertIn("Subagents are read-only", result["hookSpecificOutput"]["permissionDecisionReason"])


class CompletionGuardTests(unittest.TestCase):
    def test_shared_approval_cannot_escape_ticket_allowlist(self) -> None:
        state = active_state()
        shared_path = "scripts/new-helper.ps1"
        state["shared_write"] = {
            "approved": True,
            "paths": [shared_path],
            "reference": "exact user approval",
        }
        errors = HOOKS.validate_state(state)
        self.assertTrue(any("must also be inside allowed_write_paths" in error for error in errors))
        result = HOOKS.completion_decision(
            {}, Path.cwd(), state, [shared_path], branch="codex/s1-05-ccj"
        )
        self.assertEqual(result["decision"], "block")
        self.assertIn("Invalid stage state", result["reason"])

    def test_idle_meta_only_task_can_stop(self) -> None:
        result = HOOKS.completion_decision({}, Path.cwd(), HOOKS.default_state(), [".codex/README.md"])
        self.assertEqual(result, {})

    def test_idle_product_change_blocks_stop(self) -> None:
        result = HOOKS.completion_decision({}, Path.cwd(), HOOKS.default_state(), ["src/a.cpp"])
        self.assertEqual(result["decision"], "block")

    def test_cross_owner_change_blocks_stop(self) -> None:
        result = HOOKS.completion_decision({}, Path.cwd(), active_state(), ["include/cogito/result.hpp"])
        self.assertIn("claude-owned", result["reason"])

    def test_product_change_requires_independent_evidence(self) -> None:
        result = HOOKS.completion_decision(
            {},
            Path.cwd(),
            active_state(),
            ["src/canonical_json.cpp"],
            branch="codex/s1-05-ccj",
        )
        self.assertEqual(result["decision"], "block")

    def test_active_ticket_cannot_stop_with_clean_or_control_only_delta(self) -> None:
        for changed in ([], [".codex/stage-state.json"]):
            with self.subTest(changed=changed):
                result = HOOKS.completion_decision(
                    {}, Path.cwd(), active_state(), changed, branch="codex/s1-05-ccj"
                )
                self.assertEqual(result["decision"], "block")
                self.assertIn("phase=implementation", result["reason"])

    def test_passed_verifier_with_real_evidence_allows_stop(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            product = root / "src" / "canonical_json.cpp"
            product.parent.mkdir(parents=True)
            product.write_text("v1", encoding="utf-8")
            state = completed_state_with_evidence(root)
            result = HOOKS.completion_decision(
                {}, root, state, ["src/canonical_json.cpp"], branch="codex/s1-05-ccj"
            )
            self.assertEqual(result, {})

            product.write_text("v2", encoding="utf-8")
            result = HOOKS.completion_decision(
                {}, root, state, ["src/canonical_json.cpp"], branch="codex/s1-05-ccj"
            )
            self.assertEqual(result["decision"], "block")

    def test_evidence_content_change_invalidates_verdict(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            product = root / "src" / "canonical_json.cpp"
            product.parent.mkdir(parents=True)
            product.write_text("v1", encoding="utf-8")
            state = completed_state_with_evidence(root)
            output_path = root / EVIDENCE_REL / "command-01.log"
            output_path.write_text("tampered but nonempty\n", encoding="utf-8")
            result = HOOKS.completion_decision(
                {}, root, state, ["src/canonical_json.cpp"], branch="codex/s1-05-ccj"
            )
            self.assertIn("evidence digest changed", result["reason"])

    def test_approved_shared_content_change_invalidates_fingerprint(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            product = root / "src" / "canonical_json.cpp"
            product.parent.mkdir(parents=True)
            product.write_text("v1", encoding="utf-8")
            shared_relative = "scripts/ticket-helper.ps1"
            shared = root / shared_relative
            shared.parent.mkdir(parents=True)
            shared.write_text("v1", encoding="utf-8")
            state = completed_state_with_evidence(
                root, extra_shared_paths=(shared_relative,)
            )
            shared.write_text("v2", encoding="utf-8")
            result = HOOKS.completion_decision(
                {},
                root,
                state,
                ["src/canonical_json.cpp", shared_relative],
                branch="codex/s1-05-ccj",
            )
            self.assertIn("changed after independent verification", result["reason"])

    def test_broad_allowed_directory_fingerprints_exact_shared_intersection(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            product = root / "src" / "canonical_json.cpp"
            product.parent.mkdir(parents=True)
            product.write_text("v1", encoding="utf-8")
            shared_relative = "scripts/ownership-policy.json"
            shared = root / shared_relative
            shared.parent.mkdir(parents=True)
            shared.write_text("v1", encoding="utf-8")
            state = active_state()
            state["allowed_write_paths"].append("scripts/")
            state["shared_write"] = {
                "approved": True,
                "paths": [shared_relative],
                "reference": "exact user approval",
            }
            before = HOOKS.scope_identity(root, state)
            scoped = dict(HOOKS._scope_files(root, state))
            self.assertIn(shared_relative, scoped)
            shared.write_text("v2", encoding="utf-8")
            after = HOOKS.scope_identity(root, state)
            self.assertNotEqual(before["workspace_fingerprint"], after["workspace_fingerprint"])

    def test_scope_fingerprint_uses_unambiguous_framing(self) -> None:
        contract = b"same-contract"
        before = HOOKS._framed_scope_digest(
            contract,
            [("src/a", b"X"), ("src/b", b"Y")],
        )
        delimiter_collision = HOOKS._framed_scope_digest(
            contract,
            [("src/a", b"X\x00src/b\x00Y")],
        )
        missing = HOOKS._framed_scope_digest(contract, [("src/a", None)])
        literal_missing = HOOKS._framed_scope_digest(contract, [("src/a", b"<missing>")])
        self.assertNotEqual(before, delimiter_collision)
        self.assertNotEqual(missing, literal_missing)

    def test_empty_evidence_directory_does_not_satisfy_stop(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            product = root / "src" / "canonical_json.cpp"
            product.parent.mkdir(parents=True)
            product.write_text("v1", encoding="utf-8")
            state = active_state()
            state["phase"] = "complete"
            evidence_rel = "artifacts/verification/S1/empty"
            state["allowed_write_paths"].append(evidence_rel + "/")
            state["shared_write"] = {
                "approved": True,
                "paths": [evidence_rel + "/"],
                "reference": "user approved evidence path",
            }
            (root / evidence_rel).mkdir(parents=True)
            state["verification"].update(
                {
                    "ticket": "S1-05",
                    "commands": ["ctest --preset windows-msvc-debug"],
                    "evidence_dir": evidence_rel,
                    "manifest_file": evidence_rel + "/manifest.json",
                    "verdict_file": evidence_rel + "/verdict.json",
                }
            )
            identity = HOOKS.scope_identity(root, state)
            state["verification"].update({"verifier": "pass", **identity})
            result = HOOKS.completion_decision(
                {}, root, state, ["src/canonical_json.cpp"], branch="codex/s1-05-ccj"
            )
            self.assertEqual(result["decision"], "block")
            self.assertIn("missing or empty", result["reason"])

    def test_verdict_cannot_be_reused_for_another_ticket(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            product = root / "src" / "canonical_json.cpp"
            product.parent.mkdir(parents=True)
            product.write_text("v1", encoding="utf-8")
            state = completed_state_with_evidence(root)
            state["verification"]["ticket"] = "S1-06"
            result = HOOKS.completion_decision(
                {}, root, state, ["src/canonical_json.cpp"], branch="codex/s1-05-ccj"
            )
            self.assertIn("verification.ticket", result["reason"])

    def test_verdict_clause_ids_must_match_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            product = root / "src" / "canonical_json.cpp"
            product.parent.mkdir(parents=True)
            product.write_text("v1", encoding="utf-8")
            state = completed_state_with_evidence(root)
            verdict_path = root / state["verification"]["verdict_file"]
            verdict = json.loads(verdict_path.read_text(encoding="utf-8"))
            verdict["acceptance"][0]["id"] = "S1-05-DIFFERENT"
            verdict_path.write_text(json.dumps(verdict), encoding="utf-8")
            result = HOOKS.completion_decision(
                {}, root, state, [], branch="codex/s1-05-ccj"
            )
            self.assertIn("clause ids", result["reason"])

    def test_verdict_is_bound_to_exact_manifest_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            product = root / "src" / "canonical_json.cpp"
            product.parent.mkdir(parents=True)
            product.write_text("v1", encoding="utf-8")
            state = completed_state_with_evidence(root)
            manifest_path = root / state["verification"]["manifest_file"]
            manifest_path.write_text(manifest_path.read_text(encoding="utf-8") + "\n", encoding="utf-8")
            result = HOOKS.completion_decision(
                {}, root, state, [], branch="codex/s1-05-ccj"
            )
            self.assertIn("exact evidence manifest bytes", result["reason"])

    def test_shared_scope_expansion_invalidates_verdict(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            product = root / "src" / "canonical_json.cpp"
            product.parent.mkdir(parents=True)
            product.write_text("v1", encoding="utf-8")
            state = completed_state_with_evidence(root)
            state["allowed_write_paths"].append("scripts/new-helper.ps1")
            state["shared_write"]["paths"].append("scripts/new-helper.ps1")
            result = HOOKS.completion_decision(
                {}, root, state, [], branch="codex/s1-05-ccj"
            )
            self.assertIn("changed after independent verification", result["reason"])


class GitDeltaTests(unittest.TestCase):
    @staticmethod
    def git(root: Path, *args: str) -> str:
        completed = subprocess.run(
            ["git", "-C", str(root), *args],
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        return completed.stdout.strip()

    def test_committed_and_ignored_guarded_paths_remain_visible(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self.git(root, "init")
            self.git(root, "config", "user.name", "Guard Test")
            self.git(root, "config", "user.email", "guard@example.invalid")
            include = root / "include"
            include.mkdir()
            tracked = include / "contract.hpp"
            tracked.write_text("v1", encoding="utf-8")
            (root / ".gitignore").write_text(
                "include/ignored.hpp\nscripts/ignored.ps1\n", encoding="utf-8"
            )
            self.git(root, "add", ".gitignore", "include/contract.hpp")
            self.git(root, "commit", "-m", "baseline")
            baseline = self.git(root, "rev-parse", "HEAD")

            tracked.write_text("v2", encoding="utf-8")
            self.git(root, "add", "include/contract.hpp")
            self.git(root, "commit", "-m", "cross-owner change")
            (include / "ignored.hpp").write_text("ignored", encoding="utf-8")
            scripts = root / "scripts"
            scripts.mkdir()
            (scripts / "ignored.ps1").write_text("ignored", encoding="utf-8")

            changed = HOOKS.git_changed_paths(root, baseline)
            self.assertIn("include/contract.hpp", changed)
            self.assertIn("include/ignored.hpp", changed)
            self.assertIn("scripts/ignored.ps1", changed)


class ControlPlaneConfigTests(unittest.TestCase):
    def test_missing_ownership_policy_fails_pretool_and_stop_closed(self) -> None:
        payload = {
            "hook_event_name": "PreToolUse",
            "tool_name": "shell_command",
            "tool_input": {"command": "git --no-pager status --short"},
        }
        with mock.patch.object(HOOKS, "OWNERSHIP_POLICY_ERROR", "missing policy"):
            pre_tool = HOOKS.pre_tool_decision(
                payload, REPO_ROOT, HOOKS.default_state(), "codex/ops-bootstrap"
            )
            stop = HOOKS.completion_decision(
                {}, REPO_ROOT, HOOKS.default_state(), [], branch="codex/ops-bootstrap"
            )
        self.assertEqual(pre_tool["hookSpecificOutput"]["permissionDecision"], "deny")
        self.assertIn("ownership policy", pre_tool["hookSpecificOutput"]["permissionDecisionReason"])
        self.assertEqual(stop["decision"], "block")
        self.assertIn("ownership policy", stop["reason"])

    def test_checked_in_idle_state_matches_runtime_schema(self) -> None:
        state = json.loads((REPO_ROOT / ".codex" / "stage-state.json").read_text(encoding="utf-8"))
        self.assertEqual(HOOKS.validate_state(state), [])
        self.assertEqual(state["schema_version"], 3)

    def test_ticket_lower_bound_rejects_zero(self) -> None:
        state = active_state()
        state["active_ticket"] = "S0-00"
        state["verification"]["ticket"] = "S0-00"
        self.assertTrue(any("active_ticket" in error for error in HOOKS.validate_state(state)))
        state["active_ticket"] = "S0-01"
        state["verification"]["ticket"] = "S0-01"
        self.assertEqual(HOOKS.validate_state(state), [])

    def test_project_config_selects_only_the_documentation_mcp(self) -> None:
        config = tomllib.loads((REPO_ROOT / ".codex" / "config.toml").read_text(encoding="utf-8"))
        self.assertEqual(config["model"], "gpt-5.6-sol")
        self.assertEqual(config["model_reasoning_effort"], "ultra")
        self.assertEqual(config["agents"]["max_concurrent_threads_per_session"], 2)
        self.assertEqual(config["agents"]["max_depth"], 1)
        self.assertEqual(set(config["mcp_servers"]), {"openaiDeveloperDocs"})
        self.assertFalse(config["mcp_servers"]["openaiDeveloperDocs"]["required"])

    def test_custom_agents_are_named_ultra_and_read_only(self) -> None:
        for filename, expected_name in (
            ("default.toml", "default"),
            ("worker.toml", "worker"),
            ("explorer.toml", "explorer"),
            ("contract-auditor.toml", "contract_auditor"),
            ("exit-gate-verifier.toml", "exit_gate_verifier"),
        ):
            with self.subTest(filename=filename):
                agent = tomllib.loads(
                    (REPO_ROOT / ".codex" / "agents" / filename).read_text(encoding="utf-8")
                )
                self.assertEqual(agent["name"], expected_name)
                self.assertEqual(agent["model"], "gpt-5.6-sol")
                self.assertEqual(agent["model_reasoning_effort"], "ultra")
                self.assertEqual(agent["sandbox_mode"], "read-only")

        config = tomllib.loads((REPO_ROOT / ".codex" / "config.toml").read_text(encoding="utf-8"))
        for role in ("default", "worker", "explorer", "contract_auditor", "exit_gate_verifier"):
            self.assertIn(role, config["agents"])

    def test_windows_hooks_use_unicode_safe_worktree_portable_launcher(self) -> None:
        hooks = json.loads((REPO_ROOT / ".codex" / "hooks.json").read_text(encoding="utf-8"))
        handlers = [
            handler
            for groups in hooks["hooks"].values()
            for group in groups
            for handler in group["hooks"]
        ]
        self.assertTrue(handlers)
        for handler in handlers:
            self.assertIn("$hookPayload.cwd", handler["commandWindows"])
            self.assertIn("UTF8Encoding", handler["commandWindows"])
            self.assertNotIn(str(REPO_ROOT), handler["commandWindows"])
            self.assertNotIn("git rev-parse", handler["commandWindows"])
            self.assertIn("-B", handler["commandWindows"])
        stop_handler = hooks["hooks"]["Stop"][0]["hooks"][0]
        self.assertGreater(stop_handler["timeout"], HOOKS.STOP_GIT_TIMEOUT_CEILING_SECONDS)
        pre_tool_handler = hooks["hooks"]["PreToolUse"][0]["hooks"][0]
        self.assertGreater(pre_tool_handler["timeout"], HOOKS.GIT_QUERY_TIMEOUT_SECONDS)
        session_end_handler = hooks["hooks"]["SessionEnd"][0]["hooks"][0]
        self.assertLessEqual(session_end_handler["timeout"], 3)

    @unittest.skipUnless(sys.platform == "win32", "Windows launcher test")
    def test_actual_windows_launcher_uses_payload_repo_from_lost_process_cwd(self) -> None:
        hooks = json.loads((REPO_ROOT / ".codex" / "hooks.json").read_text(encoding="utf-8"))
        command = hooks["hooks"]["SessionStart"][0]["hooks"][0]["commandWindows"]
        payload = json.dumps(
            {"hook_event_name": "SessionStart", "cwd": str(REPO_ROOT / ".codex" / "hooks" / "tests")}
        )
        with tempfile.TemporaryDirectory() as outside:
            completed = subprocess.run(
                command,
                cwd=outside,
                input=payload,
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
                shell=True,
            )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(json.loads(completed.stdout)["hookSpecificOutput"]["hookEventName"], "SessionStart")

    @unittest.skipUnless(sys.platform == "win32", "Windows launcher test")
    def test_actual_session_end_launcher_finishes_within_supported_timeout(self) -> None:
        hooks = json.loads((REPO_ROOT / ".codex" / "hooks.json").read_text(encoding="utf-8"))
        handler = hooks["hooks"]["SessionEnd"][0]["hooks"][0]
        payload = json.dumps(
            {"hook_event_name": "SessionEnd", "cwd": str(REPO_ROOT / ".codex" / "hooks")}
        )
        started = time.perf_counter()
        completed = subprocess.run(
            handler["commandWindows"],
            cwd=REPO_ROOT,
            input=payload,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            shell=True,
            timeout=handler["timeout"],
        )
        elapsed = time.perf_counter() - started
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(completed.stdout, "")
        self.assertLess(elapsed, handler["timeout"])

    def test_cli_helpers_fall_back_to_checked_in_script_location(self) -> None:
        with tempfile.TemporaryDirectory() as outside:
            for option, expected_key in (
                ("--validate-state", "valid"),
                ("--fingerprint", "workspace_fingerprint"),
            ):
                with self.subTest(option=option):
                    completed = subprocess.run(
                        [sys.executable, "-B", str(MODULE_PATH), option],
                        cwd=outside,
                        check=False,
                        capture_output=True,
                        text=True,
                        encoding="utf-8",
                    )
                    self.assertEqual(completed.returncode, 0, completed.stderr)
                    self.assertIn(expected_key, json.loads(completed.stdout))

    def test_shared_policy_is_the_complete_runtime_ownership_source(self) -> None:
        policy = json.loads((REPO_ROOT / "scripts" / "ownership-policy.json").read_text(encoding="utf-8"))
        expected_contract = {
            "algorithm": "longest_prefix",
            "case_sensitive": False,
            "separator": "/",
            "directory_prefix_suffix": "/",
            "special_root_prefixes": ["Cogito++_"],
            "fallback_owner": "shared",
        }
        self.assertEqual(policy["guard_contract"], expected_contract)
        expected_rules = sorted(
            (
                (rule["prefix"], rule.get("guard_owner", rule["owner"]))
                for rule in policy["rules"]
            ),
            key=lambda item: len(item[0]),
            reverse=True,
        )
        self.assertEqual(HOOKS.OWNERSHIP_RULES, expected_rules)
        self.assertEqual(HOOKS.OWNERSHIP_FALLBACK, expected_contract["fallback_owner"])
        self.assertEqual(
            HOOKS.OWNERSHIP_SPECIAL_ROOT_PREFIXES,
            tuple(expected_contract["special_root_prefixes"]),
        )
        self.assertEqual(HOOKS.owner_for_path(".agents/other/asset.md"), "antigravity")
        self.assertEqual(
            HOOKS.owner_for_path(".agents/skills/cogito-stage-owner/SKILL.md"),
            "codex-control",
        )
        self.assertEqual(HOOKS.owner_for_path("unclassified.txt"), "shared")

    def test_stage_owner_skill_metadata_is_minimal_and_routable(self) -> None:
        skill_root = REPO_ROOT / ".agents" / "skills" / "cogito-stage-owner"
        content = (skill_root / "SKILL.md").read_text(encoding="utf-8").replace("\r\n", "\n")
        match = re.match(r"^---\n(.*?)\n---", content, re.DOTALL)
        self.assertIsNotNone(match)
        fields: dict[str, str] = {}
        for line in match.group(1).splitlines():
            key, separator, value = line.partition(":")
            self.assertEqual(separator, ":")
            fields[key.strip()] = value.strip()
        self.assertEqual(set(fields), {"name", "description"})
        self.assertRegex(fields["name"], r"^[a-z0-9-]{1,64}$")
        self.assertNotRegex(fields["name"], r"(^-|-$|--)")
        self.assertLessEqual(len(fields["description"]), 1024)
        self.assertNotRegex(fields["description"], r"[<>]")

        metadata = (skill_root / "agents" / "openai.yaml").read_text(encoding="utf-8")
        self.assertIn("$cogito-stage-owner", metadata)


if __name__ == "__main__":
    unittest.main()
