"""Oracle-1 tests: preflight.py reports real repository state and degrades
honestly when gh or the network is unavailable."""

from __future__ import annotations

import json
import os
import sys
import unittest
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))  # dev_common lives in scripts/dev
sys.path.insert(0, str(_HERE))          # _fixtures lives beside this file

from _fixtures import TRUNK, TempRepo, add_branch, git, merge_branch, run_tool  # noqa: E402


class PreflightFixtureTest(unittest.TestCase):
    def test_reports_master_branches_and_worktrees(self) -> None:
        with TempRepo() as fx:
            # one merged branch (behind master) and one open branch (ahead)
            add_branch(fx.repo, "feature/old", touch=["old.txt"])
            merge_branch(fx.repo, "feature/old")
            add_branch(fx.repo, "feature/new", touch=["new.txt"],
                       commit_message="work on feature/new")
            git(fx.repo, "push", "origin", "feature/new")
            git(fx.repo, "push", "origin", "feature/old")

            proc = run_tool("preflight.py", ["--path", str(fx.repo), "--json"],
                            cwd=fx.repo)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            data = json.loads(proc.stdout)

            master_sha = git(fx.repo, "rev-parse", f"origin/{TRUNK}").stdout.strip()
            self.assertEqual(data["master"]["sha"], master_sha)
            self.assertTrue(data["master"]["subject"])

            branches = {b["name"]: b for b in data["historical_branches"]}
            self.assertIn("origin/feature/new", branches)
            self.assertGreaterEqual(branches["origin/feature/new"]["ahead_vs_master"], 1)
            self.assertEqual(branches["origin/feature/new"]["behind_master"], 0)
            self.assertIn("work on feature/new",
                          branches["origin/feature/new"]["tip_subject"])
            # the merged branch is fully behind master
            self.assertEqual(branches["origin/feature/old"]["ahead_vs_master"], 0)

            # git reports long forward-slash paths while the fixture dir may be
            # an 8.3 short path; compare resolved, case-normalised forms.
            normalise = lambda p: os.path.normcase(os.path.realpath(p))  # noqa: E731
            paths = {normalise(wt.get("path", "")) for wt in data["worktrees"]}
            self.assertIn(normalise(str(fx.repo)), paths)
            for wt in data["worktrees"]:
                self.assertIsInstance(wt.get("dirty_entries"), int)

    def test_github_degrades_to_not_executed(self) -> None:
        with TempRepo() as fx:
            # the fixture has no reachable GitHub; the tool must record that
            # instead of crashing
            proc = run_tool("preflight.py", ["--path", str(fx.repo), "--json"],
                            cwd=fx.repo)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            gh = json.loads(proc.stdout)["github"]
            if not gh.get("available"):
                self.assertEqual(gh["open_prs"]["status"], "not-executed")
                self.assertTrue(gh["open_prs"]["reason"])

    def test_refuses_outside_a_repository(self) -> None:
        with TempRepo() as fx:
            outside = fx.root / "outside"
            outside.mkdir()
            proc = run_tool("preflight.py", ["--path", str(outside)], cwd=outside)
            self.assertEqual(proc.returncode, 2)
            self.assertIn("not inside a git repository", proc.stderr)


if __name__ == "__main__":
    unittest.main()
