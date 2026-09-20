"""Oracle-2 tests: new_worktree.py must fail closed on every unsafe input.

Each refusal test asserts BOTH the non-zero exit and the absence of side
effects: no branch created, no worktree registered, no directory left behind.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import unittest
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))  # dev_common lives in scripts/dev
sys.path.insert(0, str(_HERE))          # _fixtures lives beside this file

import new_worktree  # noqa: E402  (imported for the in-process rollback test)
from _fixtures import TRUNK, TempRepo, add_branch, git, run_tool  # noqa: E402


def branch_names(repo: Path) -> list[str]:
    proc = git(repo, "for-each-ref", "--format=%(refname:short)", "refs/heads")
    return sorted(ln.strip() for ln in proc.stdout.splitlines() if ln.strip())


def worktree_paths(repo: Path) -> set[str]:
    proc = git(repo, "worktree", "list", "--porcelain")
    return {ln[len("worktree "):].strip() for ln in proc.stdout.splitlines()
            if ln.startswith("worktree ")}


class HappyPathTest(unittest.TestCase):
    def test_creates_branch_and_worktree_at_base(self) -> None:
        with TempRepo() as fx:
            base = git(fx.repo, "rev-parse", TRUNK).stdout.strip()
            target = fx.root / "wt-new"
            proc = run_tool("new_worktree.py",
                            ["--path", str(target), "--branch", "track/new",
                             "--base", f"origin/{TRUNK}", "--no-fetch", "--json"],
                            cwd=fx.repo)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            data = json.loads(proc.stdout)
            self.assertEqual(data["branch"], "track/new")
            self.assertEqual(data["base_sha"], base)
            self.assertEqual(data["head"], base)
            self.assertTrue(target.is_dir())
            self.assertIn("track/new", branch_names(fx.repo))
            self.assertEqual(git(target, "rev-parse", "HEAD").stdout.strip(), base)

    def test_conflict_suffix_adopts_dated_name(self) -> None:
        with TempRepo() as fx:
            add_branch(fx.repo, "track/dup", touch=["dup.txt"])
            proc = run_tool("new_worktree.py",
                            ["--path", str(fx.root / "wt-dup"), "--branch", "track/dup",
                             "--base", f"origin/{TRUNK}", "--no-fetch",
                             "--on-name-conflict", "suffix"], cwd=fx.repo)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertIn("adopted track/dup-", proc.stderr)
            self.assertTrue(any(n.startswith("track/dup-")
                                for n in branch_names(fx.repo)))


class RefusalTest(unittest.TestCase):
    def test_duplicate_branch_refuses_and_creates_nothing(self) -> None:
        with TempRepo() as fx:
            add_branch(fx.repo, "track/existing", touch=["a.txt"])
            before = set(branch_names(fx.repo))
            wts_before = worktree_paths(fx.repo)
            target = fx.root / "wt-should-not-exist"
            proc = run_tool("new_worktree.py",
                            ["--path", str(target), "--branch", "track/existing",
                             "--base", f"origin/{TRUNK}", "--no-fetch"], cwd=fx.repo)
            self.assertEqual(proc.returncode, 2, proc.stderr)
            # the tool's own diagnostic, not git's (git's "fatal: a branch
            # named … already exists" would satisfy a sloppier assertion even
            # with this pre-check disabled)
            self.assertIn("refused: branch already exists (local branch)",
                          proc.stderr)
            self.assertEqual(set(branch_names(fx.repo)), before)
            self.assertEqual(worktree_paths(fx.repo), wts_before)
            self.assertFalse(target.exists())

    def test_existing_path_refuses(self) -> None:
        with TempRepo() as fx:
            occupied = fx.root / "occupied"
            occupied.mkdir()
            proc = run_tool("new_worktree.py",
                            ["--path", str(occupied), "--branch", "track/fresh",
                             "--base", f"origin/{TRUNK}", "--no-fetch"], cwd=fx.repo)
            self.assertEqual(proc.returncode, 2, proc.stderr)
            self.assertIn("already exists", proc.stderr)
            self.assertNotIn("track/fresh", branch_names(fx.repo))

    def test_registered_worktree_path_refuses(self) -> None:
        with TempRepo() as fx:
            target = fx.root / "wt-registered"
            first = run_tool("new_worktree.py",
                             ["--path", str(target), "--branch", "track/one",
                              "--base", f"origin/{TRUNK}", "--no-fetch"], cwd=fx.repo)
            self.assertEqual(first.returncode, 0, first.stderr)
            proc = run_tool("new_worktree.py",
                            ["--path", str(target), "--branch", "track/two",
                             "--base", f"origin/{TRUNK}", "--no-fetch"], cwd=fx.repo)
            self.assertEqual(proc.returncode, 2, proc.stderr)
            self.assertNotIn("track/two", branch_names(fx.repo))

    def test_dirty_master_refuses(self) -> None:
        with TempRepo() as fx:
            git(fx.repo, "checkout", "-b", "master")
            (fx.repo / "README.md").write_text("dirty\n", encoding="utf-8")
            before = set(branch_names(fx.repo))
            proc = run_tool("new_worktree.py",
                            ["--path", str(fx.root / "wt-dirty"), "--branch", "track/x",
                             "--base", f"origin/{TRUNK}", "--no-fetch"], cwd=fx.repo)
            self.assertEqual(proc.returncode, 2, proc.stderr)
            self.assertIn("master", proc.stderr)
            self.assertIn("dirty", proc.stderr)
            self.assertEqual(set(branch_names(fx.repo)), before)
            self.assertFalse((fx.root / "wt-dirty").exists())

    def test_clean_master_allows_bootstrap(self) -> None:
        with TempRepo() as fx:
            # The sanctioned flow: from a CLEAN master checkout, deriving a
            # branch+worktree is allowed (nothing is written to master).
            git(fx.repo, "checkout", "-b", "master")
            proc = run_tool("new_worktree.py",
                            ["--path", str(fx.root / "wt-bootstrap"), "--branch",
                             "track/bootstrap", "--base", f"origin/{TRUNK}",
                             "--no-fetch", "--json"], cwd=fx.repo)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertIn("track/bootstrap", branch_names(fx.repo))

    def test_unresolvable_base_refuses(self) -> None:
        with TempRepo() as fx:
            proc = run_tool("new_worktree.py",
                            ["--path", str(fx.root / "wt-nobase"), "--branch", "track/y",
                             "--base", "origin/does-not-exist", "--no-fetch"],
                            cwd=fx.repo)
            self.assertEqual(proc.returncode, 2, proc.stderr)
            self.assertIn("base ref does not resolve", proc.stderr)
            self.assertNotIn("track/y", branch_names(fx.repo))

    def test_path_inside_repo_refuses(self) -> None:
        with TempRepo() as fx:
            inside = fx.repo / "nested-worktree"
            proc = run_tool("new_worktree.py",
                            ["--path", str(inside), "--branch", "track/inside",
                             "--base", f"origin/{TRUNK}", "--no-fetch"], cwd=fx.repo)
            self.assertEqual(proc.returncode, 2, proc.stderr)
            self.assertIn("inside this repository", proc.stderr)
            self.assertNotIn("track/inside", branch_names(fx.repo))


class RollbackTest(unittest.TestCase):
    def test_worktree_add_failure_rolls_branch_back(self) -> None:
        """Phase-2 failure must delete the branch phase 1 just created.

        The failure is injected in-process (no PATH stub, so the test is
        deterministic on Windows too): `run` is patched to fail only for
        `git … worktree add`.
        """
        with TempRepo() as fx:
            before = set(branch_names(fx.repo))
            real_run = new_worktree.run

            def fake_run(argv, cwd=None, timeout=None):
                if "worktree" in argv and "add" in argv:
                    return subprocess.CompletedProcess(
                        argv, 1, "", "stub: worktree add failed")
                return real_run(argv, cwd=cwd, timeout=timeout)

            new_worktree.run = fake_run
            previous_cwd = Path.cwd()
            os.chdir(fx.repo)
            try:
                rc = new_worktree.main([
                    "--path", str(fx.root / "wt-rollback"), "--branch",
                    "track/rollback", "--base", f"origin/{TRUNK}", "--no-fetch"])
            finally:
                os.chdir(previous_cwd)
                new_worktree.run = real_run
            self.assertEqual(rc, 2)
            self.assertEqual(set(branch_names(fx.repo)), before,
                             "orphan branch survived a failed worktree add")
            self.assertFalse((fx.root / "wt-rollback").exists())


if __name__ == "__main__":
    unittest.main()
