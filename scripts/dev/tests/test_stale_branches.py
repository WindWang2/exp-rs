"""Tests for stale_branches.py: advisory classification, never action."""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))  # dev_common lives in scripts/dev
sys.path.insert(0, str(_HERE))          # _fixtures lives beside this file

from _fixtures import TRUNK, TempRepo, add_branch, git, merge_branch, run_tool  # noqa: E402


def classifications(data: dict) -> dict[str, str]:
    return {b["branch"]: b["classification"] for b in data["branches"]}


class StaleBranchTest(unittest.TestCase):
    def test_merged_branch_is_merged_equivalent(self) -> None:
        with TempRepo() as fx:
            add_branch(fx.repo, "feature/landed", touch=["landed.cpp"],
                       commit_message="fix: landed")
            merge_branch(fx.repo, "feature/landed", message="merge feature/landed")
            git(fx.repo, "push", "origin", "feature/landed")
            git(fx.repo, "fetch", "origin")
            proc = run_tool("stale_branches.py", ["--json"], cwd=fx.repo)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            data = json.loads(proc.stdout)
            self.assertEqual(classifications(data)["origin/feature/landed"],
                             "merged-equivalent")
            # advisory only: the branch still exists afterwards
            self.assertIn("origin/feature/landed",
                          git(fx.repo, "branch", "-r").stdout)
            self.assertIn("never", data["note"])

    def test_branch_with_files_master_lacks_is_unmerged_increment(self) -> None:
        with TempRepo() as fx:
            add_branch(fx.repo, "feature/orphan-asset", touch=["tests/test_orphan.cpp"],
                       commit_message="test: orphan asset")
            git(fx.repo, "push", "origin", "feature/orphan-asset")
            proc = run_tool("stale_branches.py",
                            ["--branch", "feature/orphan-asset", "--json"], cwd=fx.repo)
            data = json.loads(proc.stdout)
            entry = data["branches"][0]
            self.assertEqual(entry["classification"], "unmerged-increment")
            self.assertIn("tests/test_orphan.cpp", entry["unmerged_files"])

    def test_far_behind_branch_is_diverged_stale(self) -> None:
        with TempRepo() as fx:
            add_branch(fx.repo, "feature/stale", touch=["stale.cpp"],
                       commit_message="chore: stale")
            git(fx.repo, "push", "origin", "feature/stale")
            # push 60 trunk commits so the branch is far behind master
            git(fx.repo, "checkout", TRUNK)
            env = {"GIT_AUTHOR_NAME": "Fixture", "GIT_AUTHOR_EMAIL": "f@x.invalid",
                   "GIT_COMMITTER_NAME": "Fixture", "GIT_COMMITTER_EMAIL": "f@x.invalid"}
            for i in range(60):
                path = fx.repo / f"trunk_{i}.txt"
                path.write_text(f"{i}\n", encoding="utf-8")
                git(fx.repo, "add", "-A")
                proc = git(fx.repo, "commit", "-m", f"trunk {i}", env=env)
                self.assertEqual(proc.returncode, 0, proc.stderr)
            git(fx.repo, "push", "origin", TRUNK)
            git(fx.repo, "fetch", "origin", "--prune")
            proc = run_tool("stale_branches.py", ["--json"], cwd=fx.repo)
            entry = {b["branch"]: b for b in json.loads(proc.stdout)["branches"]}
            self.assertGreaterEqual(entry["origin/feature/stale"]["behind_master"], 50)
            self.assertIn(entry["origin/feature/stale"]["classification"],
                          ("diverged-stale", "unmerged-increment"))

    def test_no_action_taken_on_any_branch(self) -> None:
        with TempRepo() as fx:
            add_branch(fx.repo, "feature/keep", touch=["keep.cpp"],
                       commit_message="chore: keep me")
            git(fx.repo, "push", "origin", "feature/keep")
            before = set(git(fx.repo, "branch", "-r").stdout.split())
            run_tool("stale_branches.py", [], cwd=fx.repo)
            after = set(git(fx.repo, "branch", "-r").stdout.split())
            self.assertEqual(before, after)


if __name__ == "__main__":
    unittest.main()
