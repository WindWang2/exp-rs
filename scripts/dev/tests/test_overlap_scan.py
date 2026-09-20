"""Tests for overlap_scan.py: file-level evidence, never equivalence."""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))  # dev_common lives in scripts/dev
sys.path.insert(0, str(_HERE))          # _fixtures lives beside this file

from _fixtures import TRUNK, TempRepo, add_branch, git, run_tool  # noqa: E402


class OverlapScanTest(unittest.TestCase):
    def test_detects_branch_touching_planned_paths(self) -> None:
        with TempRepo() as fx:
            add_branch(fx.repo, "feature/geo", touch=["src/geo/handler.cpp"])
            git(fx.repo, "push", "origin", "feature/geo")
            proc = run_tool("overlap_scan.py",
                            ["--base", f"origin/{TRUNK}", "src/geo", "--json"],
                            cwd=fx.repo)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            data = json.loads(proc.stdout)
            hits = [b["branch"] for b in data["remote_branches"]]
            self.assertIn("origin/feature/geo", hits)
            entry = next(b for b in data["remote_branches"]
                         if b["branch"] == "origin/feature/geo")
            self.assertEqual(entry["overlapping_paths"], ["src/geo/handler.cpp"])

    def test_no_overlap_when_paths_disjoint(self) -> None:
        with TempRepo() as fx:
            add_branch(fx.repo, "feature/geo", touch=["src/geo/handler.cpp"])
            git(fx.repo, "push", "origin", "feature/geo")
            proc = run_tool("overlap_scan.py",
                            ["--base", f"origin/{TRUNK}", "docs/", "--json"],
                            cwd=fx.repo)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            data = json.loads(proc.stdout)
            self.assertEqual(data["overlap_count"], 0)
            self.assertEqual(data["remote_branches"], [])

    def test_evidence_only_never_judges_equivalence(self) -> None:
        with TempRepo() as fx:
            add_branch(fx.repo, "feature/geo", touch=["src/geo/handler.cpp"])
            git(fx.repo, "push", "origin", "feature/geo")
            proc = run_tool("overlap_scan.py",
                            ["--base", f"origin/{TRUNK}", "src/geo", "--json"],
                            cwd=fx.repo)
            text = proc.stdout
            for banned in ("equivalent", "duplicate", "same change", "conflict "):
                self.assertNotIn(banned, text.lower())
            data = json.loads(proc.stdout)
            for entry in data["remote_branches"]:
                self.assertEqual(set(entry), {"branch", "ahead_vs_base",
                                              "behind_base", "overlapping_paths"})

    def test_fail_on_overlap_flag(self) -> None:
        with TempRepo() as fx:
            add_branch(fx.repo, "feature/geo", touch=["src/geo/handler.cpp"])
            git(fx.repo, "push", "origin", "feature/geo")
            proc = run_tool("overlap_scan.py",
                            ["--base", f"origin/{TRUNK}", "src/geo",
                             "--fail-on-overlap"], cwd=fx.repo)
            self.assertEqual(proc.returncode, 2)


if __name__ == "__main__":
    unittest.main()
