"""Oracle-4 tests: review_pack.py produces complete, reproducible PR evidence."""

from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))  # dev_common lives in scripts/dev
sys.path.insert(0, str(_HERE))          # _fixtures lives beside this file

from _fixtures import TRUNK, TempRepo, add_branch, git, run_tool  # noqa: E402

REQUIRED = ["Baseline", "Scope", "Non-goals", "Design", "Issue mapping", "Tests",
            "Resources", "Review disposition", "Known limitations", "Conflict hotspots"]


def strip_generated(text: str) -> str:
    return "\n".join(ln for ln in text.splitlines()
                     if not ln.startswith("Generated: "))


class ReviewPackTest(unittest.TestCase):
    def test_pack_has_every_required_section_and_evidence(self) -> None:
        with TempRepo() as fx:
            add_branch(fx.repo, "feature/pack",
                       touch=["src/geo/handler.cpp", "src/geo/handler.h",
                              "tests/test_pack.cpp", "docs/design.md", "README.md"],
                       commit_message="feat: pack fixture")
            proc = run_tool("review_pack.py",
                            ["--base", f"origin/{TRUNK}", "--head", "feature/pack",
                             "--title", "feat: pack fixture"],
                            cwd=fx.repo)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            body = proc.stdout
            for section in REQUIRED:
                self.assertIn(f"## {section}", body, section)
            self.assertIn("src/geo/handler.cpp", body)
            self.assertIn("tests/test_pack.cpp", body)
            self.assertIn("public_headers", body)  # grouping taxonomy present
            self.assertIn("feat: pack fixture", body)  # commit list
            self.assertIn("Commits", body)
            self.assertIn("Diff stat", body)
            # the fixture is whitespace-clean
            payload = run_tool("review_pack.py",
                               ["--base", f"origin/{TRUNK}", "--head", "feature/pack",
                                "--json"], cwd=fx.repo)
            self.assertEqual(payload.returncode, 0, payload.stderr)
            self.assertTrue(json.loads(payload.stdout)["diff_check"]["clean"])

    def test_reproducible_modulo_generated_line(self) -> None:
        with TempRepo() as fx:
            add_branch(fx.repo, "feature/pack2", touch=["src/geo/x.cpp"],
                       commit_message="feat: reproducibility")
            args = ["--base", f"origin/{TRUNK}", "--head", "feature/pack2",
                    "--title", "t"]
            first = run_tool("review_pack.py", args, cwd=fx.repo)
            second = run_tool("review_pack.py", args, cwd=fx.repo)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(strip_generated(first.stdout),
                             strip_generated(second.stdout))

    def test_out_file_written_and_self_check_passes(self) -> None:
        with TempRepo() as fx:
            add_branch(fx.repo, "feature/pack3", touch=["src/geo/y.cpp"],
                       commit_message="feat: pack3")
            out = fx.root / "PR_PACK.md"
            proc = run_tool("review_pack.py",
                            ["--base", f"origin/{TRUNK}", "--head", "feature/pack3",
                             "--out", str(out)], cwd=fx.repo)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertTrue(out.exists())
            for section in REQUIRED:
                self.assertIn(f"## {section}", out.read_text(encoding="utf-8"))

    def test_missing_base_ref_fails_cleanly(self) -> None:
        with TempRepo() as fx:
            proc = run_tool("review_pack.py",
                            ["--base", "origin/nope", "--head", "HEAD"], cwd=fx.repo)
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("failed", proc.stderr.lower())


if __name__ == "__main__":
    unittest.main()
