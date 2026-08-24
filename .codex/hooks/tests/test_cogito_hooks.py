from __future__ import annotations

import importlib.util
import io
import json
import re
import subprocess
import sys
import tempfile
import tomllib
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[3]
HOOK_PATH = REPO_ROOT / ".codex" / "hooks" / "cogito_hooks.py"
GEMINI_GUARD_PATH = REPO_ROOT / "scripts" / "guard-scope.ps1"
SPEC = importlib.util.spec_from_file_location("cogito_hooks", HOOK_PATH)
assert SPEC and SPEC.loader
HOOKS = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = HOOKS
SPEC.loader.exec_module(HOOKS)
POLICY = HOOKS.load_policy(REPO_ROOT / "scripts" / "ownership-policy.json")


def payload(tool: str, value, **extra):
    result = {"tool_name": tool, "tool_input": value}
    result.update(extra)
    return result


def denied(result: dict) -> bool:
    return result.get("hookSpecificOutput", {}).get("permissionDecision") == "deny"


class PolicyTests(unittest.TestCase):
    def test_checked_in_policy_is_valid(self):
        self.assertEqual(POLICY["guard_contract"]["fallback_owner"], "deny")
        self.assertEqual(set(POLICY["owners"]), {"gemini", "codex"})

    def test_gemini_contract_paths(self):
        for path in ("include/cogito/x.hpp", "docs/spec.md", "config/policy.json", ".agents/rules/x.md"):
            with self.subTest(path=path):
                self.assertEqual(HOOKS.owner_for_path(path, POLICY), "gemini")

    def test_codex_implementation_paths(self):
        for path in (
            "src/core.cpp", "tests/core/test.cpp", "tests/web/e2e.js",
            "tools/web_dashboard/app.js", "tools/mock_server/server.js",
            "tools/cli/main.cpp", "bindings/python.cpp", "cmake/Cogito.cmake",
            "scripts/check.ps1", "CMakeLists.txt", "vcpkg.json",
        ):
            with self.subTest(path=path):
                self.assertEqual(HOOKS.owner_for_path(path, POLICY), "codex")

    def test_stage_owner_carveout_is_codex(self):
        self.assertEqual(
            HOOKS.owner_for_path(".agents/skills/cogito-stage-owner/SKILL.md", POLICY),
            "codex",
        )

    def test_readme_is_gemini(self):
        self.assertEqual(HOOKS.owner_for_path("README.md", POLICY), "gemini")

    def test_unknown_path_is_denied(self):
        self.assertEqual(HOOKS.owner_for_path("random.bin", POLICY), "deny")

    def test_case_insensitive_matching(self):
        self.assertEqual(HOOKS.owner_for_path("DoCs/Plan.md", POLICY), "gemini")
        self.assertEqual(HOOKS.owner_for_path("ToOlS/Web_Dashboard/App.js", POLICY), "codex")

    def test_prefix_boundary_is_exact(self):
        self.assertEqual(HOOKS.owner_for_path("docs-evil/x.md", POLICY), "deny")
        self.assertEqual(HOOKS.owner_for_path("AGENTS.md.bak", POLICY), "deny")

    def test_bom_policy_is_supported(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "policy.json"
            path.write_text(json.dumps(POLICY), encoding="utf-8-sig")
            loaded = HOOKS.load_policy(path)
            self.assertEqual(loaded["version"], 2)

    def test_missing_policy_fails_closed(self):
        with self.assertRaises(HOOKS.GuardError):
            HOOKS.load_policy(Path("does-not-exist.json"))

    def test_corrupt_policy_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "policy.json"
            path.write_text("{", encoding="utf-8")
            with self.assertRaises(HOOKS.GuardError):
                HOOKS.load_policy(path)

    def test_invalid_owner_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            value = json.loads(json.dumps(POLICY))
            value["rules"][0]["owner"] = "unknown-agent"
            path = Path(temp) / "policy.json"
            path.write_text(json.dumps(value), encoding="utf-8")
            with self.assertRaises(HOOKS.GuardError):
                HOOKS.load_policy(path)

    def test_policy_contract_cannot_be_relaxed(self):
        variants = (
            ("version", 1),
            ("fallback", "codex"),
            ("extra_owner", "claude"),
        )
        for name, value in variants:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                candidate = json.loads(json.dumps(POLICY))
                if name == "version":
                    candidate["version"] = value
                elif name == "fallback":
                    candidate["guard_contract"]["fallback_owner"] = value
                else:
                    candidate["owners"][value] = "unexpected"
                path = Path(temp) / "policy.json"
                path.write_text(json.dumps(candidate), encoding="utf-8")
                with self.assertRaises(HOOKS.GuardError):
                    HOOKS.load_policy(path)


class PathTests(unittest.TestCase):
    def test_relative_path_normalizes(self):
        self.assertEqual(HOOKS.normalize_repo_path("src\\core.cpp", REPO_ROOT), "src/core.cpp")

    def test_absolute_inside_path_normalizes(self):
        target = REPO_ROOT / "tests" / "core.cpp"
        self.assertEqual(HOOKS.normalize_repo_path(str(target), REPO_ROOT), "tests/core.cpp")

    def test_absolute_outside_path_is_not_owned(self):
        with tempfile.TemporaryDirectory() as temp:
            self.assertIsNone(HOOKS.normalize_repo_path(str(Path(temp) / "x"), REPO_ROOT))

    def test_parent_traversal_is_blocked(self):
        with self.assertRaisesRegex(HOOKS.GuardError, "parent traversal"):
            HOOKS.normalize_repo_path("../outside", REPO_ROOT)

    def test_nul_is_blocked(self):
        with self.assertRaisesRegex(HOOKS.GuardError, "NUL"):
            HOOKS.normalize_repo_path("src/a\x00.cpp", REPO_ROOT)

    def test_ads_is_blocked(self):
        with self.assertRaisesRegex(HOOKS.GuardError, "alternate"):
            HOOKS.normalize_repo_path("src/a.cpp:stream", REPO_ROOT)

    def test_device_name_is_blocked(self):
        with self.assertRaisesRegex(HOOKS.GuardError, "device"):
            HOOKS.normalize_repo_path("src/NUL.txt", REPO_ROOT)

    def test_trailing_dot_is_blocked(self):
        with self.assertRaisesRegex(HOOKS.GuardError, "trailing"):
            HOOKS.normalize_repo_path("src/name.", REPO_ROOT)

    def test_git_internals_are_blocked(self):
        with self.assertRaisesRegex(HOOKS.GuardError, ".git"):
            HOOKS.normalize_repo_path(".git/config", REPO_ROOT)


class WriteBoundaryTests(unittest.TestCase):
    def decision(self, value, tool="Write", **extra):
        return HOOKS.pre_tool_decision(payload(tool, value, **extra), REPO_ROOT, POLICY)

    def test_codex_source_write_allowed(self):
        self.assertEqual(self.decision({"file_path": "src/core.cpp"}), {})

    def test_codex_web_test_write_allowed(self):
        self.assertEqual(self.decision({"file_path": "tests/web/e2e.js"}), {})

    def test_codex_dashboard_write_allowed(self):
        self.assertEqual(self.decision({"file_path": "tools/web_dashboard/app.js"}), {})

    def test_gemini_header_write_denied(self):
        self.assertTrue(denied(self.decision({"file_path": "include/cogito/api.hpp"})))

    def test_gemini_doc_write_denied(self):
        self.assertTrue(denied(self.decision({"file_path": "docs/spec.md"})))

    def test_gemini_agents_write_denied(self):
        self.assertTrue(denied(self.decision({"file_path": ".agents/rules/x.md"})))

    def test_codex_skill_carveout_allowed(self):
        self.assertEqual(
            self.decision({"file_path": ".agents/skills/cogito-stage-owner/SKILL.md"}),
            {},
        )

    def test_unknown_path_denied(self):
        self.assertTrue(denied(self.decision({"file_path": "unknown.file"})))

    def test_outside_repository_write_denied(self):
        with tempfile.TemporaryDirectory() as temp:
            self.assertTrue(denied(self.decision({"file_path": str(Path(temp) / "x.cpp")})))

    def test_missing_direct_path_denied(self):
        self.assertTrue(denied(self.decision({})))

    def test_subagent_direct_write_denied(self):
        self.assertTrue(denied(self.decision({"file_path": "src/core.cpp"}, is_subagent=True)))

    def test_agent_id_marks_subagent_write_denied(self):
        self.assertTrue(denied(self.decision({"file_path": "src/core.cpp"}, agent_id="reviewer-1")))

    def test_apply_patch_codex_path_allowed(self):
        patch = "*** Begin Patch\n*** Update File: src/core.cpp\n@@\n-x\n+y\n*** End Patch"
        self.assertEqual(self.decision(patch, tool="apply_patch"), {})

    def test_apply_patch_mixed_scope_denied(self):
        patch = (
            "*** Begin Patch\n"
            "*** Update File: src/core.cpp\n"
            "*** Update File: docs/spec.md\n"
            "*** End Patch"
        )
        self.assertTrue(denied(self.decision(patch, tool="apply_patch")))

    def test_move_checks_source_and_destination(self):
        self.assertEqual(
            self.decision({"source": "src/a.cpp", "destination": "src/b.cpp"}, tool="move_file"),
            {},
        )
        self.assertTrue(
            denied(self.decision({"source": "src/a.cpp", "destination": "docs/a.md"}, tool="move_file"))
        )
        self.assertTrue(denied(self.decision({"source": "src/a.cpp"}, tool="move_file")))

    def test_main_branch_direct_write_denied(self):
        with mock.patch.object(HOOKS, "current_branch", return_value="main"):
            self.assertTrue(denied(self.decision({"file_path": "src/core.cpp"})))

    def test_unknown_branch_direct_write_denied(self):
        with mock.patch.object(HOOKS, "current_branch", return_value=None):
            self.assertTrue(denied(self.decision({"file_path": "src/core.cpp"})))

    def test_replace_file_content_is_guarded(self):
        for tool in ("replace_file_content", "replace_in_file", "replace", "write_file"):
            with self.subTest(tool=tool):
                self.assertTrue(denied(self.decision({"file_path": "docs/spec.md"}, tool=tool)))


class ShellSafetyTests(unittest.TestCase):
    def decision(self, command: str, subagent: bool = False):
        return HOOKS.shell_decision(command, REPO_ROOT, POLICY, subagent)

    def test_read_only_commands_allowed(self):
        for command in (
            "rg TODO src",
            "git status --short",
            "git diff --check",
            "Get-Content docs/spec.md",
            "rg TODO docs 2>$null",
            "rg TODO docs 2>/dev/null",
        ):
            with self.subTest(command=command):
                self.assertEqual(self.decision(command), {})

    def test_build_and_test_commands_allowed(self):
        for command in (
            "cmake --preset windows-msvc-debug",
            "cmake --build --preset windows-msvc-debug",
            "ctest --preset windows-msvc-debug",
            "ninja -C build",
            'py -3 -B -m unittest discover -s tests -p "test_*.py"',
            "py -3 -B .codex/hooks/cogito_hooks.py --validate-policy",
            "npm test",
            "npm run build",
        ):
            with self.subTest(command=command):
                self.assertEqual(self.decision(command), {})

    def test_gemini_file_shell_mutation_denied(self):
        for command in (
            "Remove-Item docs/spec.md",
            "Remove-Item -Recurse docs",
            "Set-Content include/cogito/api.hpp x",
            "git add .agents/rules/x.md",
        ):
            with self.subTest(command=command):
                self.assertTrue(denied(self.decision(command)))

    def test_shell_filesystem_mutation_requires_exact_path_tool(self):
        for command in (
            "Remove-Item src/generated.tmp",
            "Remove-Item -Recurse -Force src",
            "Set-Content $p x",
            "git add -- src/core.cpp",
        ):
            with self.subTest(command=command):
                self.assertTrue(denied(self.decision(command)))

    def test_subagent_shell_mutation_denied(self):
        self.assertTrue(denied(self.decision("Remove-Item src/generated.tmp", subagent=True)))

    def test_subagent_shell_is_explicit_read_only_allowlist(self):
        for command in (
            "rg TODO src",
            "git status --short",
            "git diff --check",
            "Get-Content docs/spec.md",
        ):
            with self.subTest(command=command):
                self.assertEqual(self.decision(command, subagent=True), {})

    def test_subagent_indirect_and_build_commands_denied(self):
        for command in (
            "py mutate.py",
            "node mutate.js",
            "cmake -P mutate.cmake",
            "git commit -m test",
            "git add src/core.cpp",
        ):
            with self.subTest(command=command):
                self.assertTrue(denied(self.decision(command, subagent=True)))

    def test_git_external_execution_options_denied(self):
        for command in (
            "git diff --ext-diff",
            "git show --textconv HEAD:x",
            "git grep -Ocalc needle",
            "git grep --open-files-in-pager=calc needle",
            "git submodule foreach calc",
            "git filter-branch -- --all",
        ):
            with self.subTest(command=command):
                self.assertTrue(denied(self.decision(command)))

    def test_unlisted_shell_mutation_and_runner_denied(self):
        for command in (
            "[IO.File]::WriteAllText('docs/x','x')",
            "Get-Content x | Tee-Object -FilePath docs/x",
            "clang-format -i include/cogito/x.hpp",
            "clang-format --dry-run --Werror -i include/cogito/x.hpp",
            "npm run mutate",
            "$env:GIT_ASKPASS='tools/helper'; git push origin feature",
            "ctest --preset x\n[IO.File]::WriteAllText('docs/x','x')",
            "ninja -C build\r\n[IO.File]::WriteAllText('docs/x','x')",
            "git commit -m ([IO.File]::WriteAllText('docs/x','x'))",
            "git commit -m $ExecutionContext.InvokeCommand.InvokeScript('whoami')",
            "pytest ([IO.File]::WriteAllText('docs/x','x'))",
            "pytest @([IO.File]::WriteAllText('docs/x','x'))",
            "node --test ([System.Diagnostics.Process]::Start('calc'))",
            "ctest --preset ([IO.File]::WriteAllText('docs/x','x'))",
            "Get-Content ([IO.File]::WriteAllText('docs/x','x'))",
            "git restore --staged $path",
        ):
            with self.subTest(command=command):
                self.assertTrue(denied(self.decision(command)))

    def test_opaque_inline_commands_denied(self):
        for command in (
            'python -c "open(\'src/x\',\'w\').write(\'x\')"',
            "py mutate.py",
            "node mutate.js",
            "cmake -P mutate.cmake",
            'powershell -Command "Set-Content src/x y"',
            "curl https://example.test/x -o src/x",
        ):
            with self.subTest(command=command):
                self.assertTrue(denied(self.decision(command)))

    def test_destructive_git_denied(self):
        for command in (
            "git reset --hard HEAD",
            "git clean -fd",
            "git update-ref -d refs/heads/x",
            "git branch -D feature",
            "git restore src/core.cpp",
            "git restore --staged --worktree src/core.cpp",
        ):
            with self.subTest(command=command):
                self.assertTrue(denied(self.decision(command)))

    def test_staged_only_restore_is_allowed(self):
        self.assertEqual(self.decision("git restore --staged src/core.cpp"), {})

    def test_broad_filesystem_deletion_denied(self):
        for command in ("rm -rf .", "Remove-Item -Recurse -Force .", "rmdir /s C:\\"):
            with self.subTest(command=command):
                self.assertTrue(denied(self.decision(command)))

    def test_force_and_protected_push_denied(self):
        for command in (
            "git push --force origin feature",
            "git push -fu origin feature",
            "git push origin main",
            "git push --delete origin feature",
        ):
            with self.subTest(command=command):
                self.assertTrue(denied(self.decision(command)))

    def test_feature_branch_commit_and_push_not_blanket_denied(self):
        with mock.patch.object(HOOKS, "current_branch", return_value="codex/feature"), mock.patch.object(
            HOOKS, "staged_paths", return_value=["src/core.cpp"]
        ):
            self.assertEqual(self.decision("git commit -m test"), {})
        with mock.patch.object(HOOKS, "current_branch", return_value="codex/feature"):
            self.assertEqual(self.decision("git push origin feature"), {})

    def test_unreviewable_git_merge_denied(self):
        self.assertTrue(denied(self.decision("git merge --no-edit other")))

    def test_protected_branch_implicit_publication_denied(self):
        with mock.patch.object(HOOKS, "current_branch", return_value="main"):
            self.assertTrue(denied(self.decision("git push origin")))
            self.assertTrue(denied(self.decision("git push -u origin HEAD")))
            self.assertTrue(denied(self.decision("git commit -m test")))

    def test_git_helper_and_repository_override_denied(self):
        for command in (
            "git -c core.hooksPath=tools/malicious commit -m test",
            "git -ccore.hooksPath=tools/malicious commit -m test",
            "git --config-env=core.hooksPath=HOOK_PATH commit -m test",
            "git --exec-path=tools/git push origin feature",
            "git --git-dir=.git status",
            "git --work-tree=. status",
            "GIT_ASKPASS=tools/helper git push origin feature",
            "git commit --no-verify -m test",
            "git --literal-pathspecs -c core.hooksPath=tools/malicious commit -m test",
            "git --literal-pathspecs -C .. commit -m test",
            "git --literal-pathspecs --exec-path=tools/git push origin feature",
        ):
            with self.subTest(command=command):
                self.assertTrue(denied(self.decision(command)))

    def test_commit_all_forms_denied(self):
        for command in (
            "git commit -a -m test",
            "git commit -am test",
            "git commit --all -m test",
            "git commit -m test docs/spec.md",
            "git commit --pathspec-from-file=paths.txt -m test",
            "git commit",
        ):
            with self.subTest(command=command):
                self.assertTrue(denied(self.decision(command)))

    def test_commit_with_gemini_path_denied(self):
        with mock.patch.object(HOOKS, "staged_paths", return_value=["src/core.cpp", "docs/spec.md"]):
            self.assertTrue(denied(self.decision("git commit -m test")))

    def test_nul_delimited_staged_path_preserves_leading_space(self):
        with mock.patch.object(HOOKS, "_run_git", return_value=" src/file\0") as runner:
            self.assertEqual(HOOKS.staged_paths(REPO_ROOT), [" src/file"])
            self.assertFalse(runner.call_args.kwargs["strip"])
        with mock.patch.object(HOOKS, "current_branch", return_value="codex/feature"), mock.patch.object(
            HOOKS, "staged_paths", return_value=[" src/file"]
        ):
            self.assertTrue(denied(self.decision("git commit -m test")))


class HookAndCliTests(unittest.TestCase):
    def test_codex_toml_configuration_parses(self):
        for relative in (
            ".codex/config.toml",
            ".codex/agents/contract-auditor.toml",
            ".codex/agents/exit-gate-verifier.toml",
        ):
            with self.subTest(path=relative):
                tomllib.loads((REPO_ROOT / relative).read_text(encoding="utf-8"))

    def test_session_context_names_two_agents(self):
        stream = io.StringIO()
        with redirect_stdout(stream):
            result = HOOKS.run_hook({"hook_event_name": "SessionStart", "cwd": str(REPO_ROOT)})
        self.assertEqual(result, 0)
        value = json.loads(stream.getvalue())
        context = value["hookSpecificOutput"]["additionalContext"]
        self.assertIn("Gemini", context)
        self.assertIn("Codex", context)

    def test_subagent_context_is_read_only(self):
        stream = io.StringIO()
        with redirect_stdout(stream):
            HOOKS.run_hook({"hook_event_name": "SubagentStart", "cwd": str(REPO_ROOT)})
        self.assertIn("read-only", stream.getvalue())

    def test_stop_event_is_noop(self):
        stream = io.StringIO()
        with redirect_stdout(stream):
            result = HOOKS.run_hook({"hook_event_name": "Stop", "cwd": str(REPO_ROOT)})
        self.assertEqual(result, 0)
        self.assertEqual(stream.getvalue(), "")

    def test_validate_policy_cli(self):
        completed = subprocess.run(
            [sys.executable, "-B", str(HOOK_PATH), "--validate-policy"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(json.loads(completed.stdout)["model"], "gemini-codex")


@unittest.skipUnless(sys.platform == "win32", "Gemini guard is a Windows PowerShell hook")
class GeminiGuardTests(unittest.TestCase):
    def invoke(self, value: dict | str) -> dict:
        raw = value if isinstance(value, str) else json.dumps(value)
        completed = subprocess.run(
            [
                "powershell.exe", "-NoLogo", "-NoProfile", "-NonInteractive",
                "-ExecutionPolicy", "Bypass", "-File", str(GEMINI_GUARD_PATH),
            ],
            cwd=REPO_ROOT,
            input=raw,
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        return json.loads(completed.stdout)

    @staticmethod
    def call(tool: str, **arguments) -> dict:
        return {"toolCall": {"name": tool, "args": arguments}}

    def test_empty_payload_fails_closed(self):
        self.assertEqual(self.invoke("")["decision"], "deny")

    def test_gemini_exact_paths_allowed(self):
        for path in (
            "docs/new.md",
            "docs/proposal/Cogito++_구현명세서.md",
            "include/cogito/new.hpp",
            "DoCs/New.md",
        ):
            with self.subTest(path=path):
                self.assertEqual(
                    self.invoke(self.call("write_to_file", TargetFile=path))["decision"],
                    "allow",
                )

    def test_codex_and_unknown_paths_denied(self):
        for path in ("src/new.cpp", "tests/web/new.test.js", "unknown.file"):
            with self.subTest(path=path):
                self.assertEqual(
                    self.invoke(self.call("write_to_file", TargetFile=path))["decision"],
                    "deny",
                )

    def test_outside_repository_path_denied(self):
        with tempfile.TemporaryDirectory() as temp:
            result = self.invoke(self.call("write_to_file", TargetFile=str(Path(temp) / "x.md")))
        self.assertEqual(result["decision"], "deny")

    def test_missing_path_and_apply_patch_denied(self):
        self.assertEqual(self.invoke(self.call("write_to_file"))["decision"], "deny")
        self.assertEqual(self.invoke(self.call("apply_patch", patch="x"))["decision"], "deny")

    def test_move_checks_source_and_destination(self):
        allowed = self.call("move_file", source="docs/a.md", destination="docs/b.md")
        mixed = self.call("move_file", source="docs/a.md", destination="src/b.cpp")
        self.assertEqual(self.invoke(allowed)["decision"], "allow")
        self.assertEqual(self.invoke(mixed)["decision"], "deny")

    def test_unrecognized_write_tool_denied(self):
        self.assertEqual(self.invoke(self.call("mystery_writer", TargetFile="docs/a.md"))["decision"], "deny")

    def test_hook_matcher_covers_supported_write_aliases(self):
        hook = json.loads((REPO_ROOT / ".agents" / "hooks.json").read_text(encoding="utf-8-sig"))
        matcher = hook["boundary-guard"]["PreToolUse"][0]["matcher"]
        for tool in (
            "write", "write_file", "write_to_file", "replace", "replace_in_file",
            "replace_file_content", "edit", "create", "create_file", "delete",
            "delete_file", "remove", "remove_file", "move", "move_file", "apply_patch",
        ):
            with self.subTest(tool=tool):
                self.assertIsNotNone(re.fullmatch(matcher, tool))

if __name__ == "__main__":
    unittest.main()
