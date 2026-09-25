"""Tests for narrow_targets.py: mechanical changed-path → narrow target map.

The mapping must come from the wiring text only — no path table. Each test
builds a tiny repo whose CMake wiring names the targets explicitly, then
asserts the mapping, the ctest suggestion, the wiring-oracle inclusion for
CMake-script changes and the `unwired` report for paths the wiring does not
reach.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))
sys.path.insert(0, str(_HERE))

from _fixtures import git, run_tool  # noqa: E402

ROOT_CMAKE = """\
cmake_minimum_required(VERSION 3.20)
project(fx CXX)
add_subdirectory(src/lib)
add_subdirectory(tests)
"""

LIB_CMAKE = """\
add_library(fxlib a.cpp b.cpp)
target_sources(fxlib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/deep/c.cpp)
"""

TESTS_CMAKE = """\
function(sicnu_fx_add_test NAME)
  add_executable(${NAME} ${NAME}.cpp)
endfunction()
sicnu_fx_add_test(test_a)
"""


def make_repo(root: Path) -> Path:
    (root / "src" / "lib" / "deep").mkdir(parents=True)
    (root / "tests").mkdir(parents=True)
    proc = git(root, "init", "--quiet")
    assert proc.returncode == 0, proc.stderr
    (root / "CMakeLists.txt").write_text(ROOT_CMAKE, encoding="utf-8")
    (root / "src" / "lib" / "CMakeLists.txt").write_text(LIB_CMAKE, encoding="utf-8")
    (root / "src" / "lib" / "a.cpp").write_text("int a;\n", encoding="utf-8")
    (root / "src" / "lib" / "b.cpp").write_text("int b;\n", encoding="utf-8")
    (root / "src" / "lib" / "deep" / "c.cpp").write_text("int c;\n", encoding="utf-8")
    (root / "tests" / "CMakeLists.txt").write_text(TESTS_CMAKE, encoding="utf-8")
    (root / "tests" / "test_a.cpp").write_text("int main(){return 0;}\n", encoding="utf-8")
    return root


class NarrowTargetsTest(unittest.TestCase):
    def map(self, repo: Path, *paths: str) -> dict:
        proc = run_tool("narrow_targets.py", [*paths, "--json"], cwd=repo)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        return json.loads(proc.stdout)

    def test_source_maps_to_its_target(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(Path(tmp))
            data = self.map(repo, "src/lib/a.cpp")
            self.assertEqual(data["build_targets"], ["fxlib"])
            self.assertFalse(data["target_details"]["fxlib"]["weak"])
            self.assertIsNone(data["ctest_suggestion"])

    def test_current_dir_variable_source_maps(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(Path(tmp))
            data = self.map(repo, "src/lib/deep/c.cpp")
            self.assertIn("fxlib", data["build_targets"])

    def test_registered_test_source_maps_and_suggests_ctest(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(Path(tmp))
            data = self.map(repo, "tests/test_a.cpp")
            self.assertIn("test_a", data["build_targets"])
            self.assertEqual(data["ctest_targets"], ["test_a"])
            self.assertEqual(data["ctest_suggestion"],
                             "ctest --test-dir <build> -R '^(test_a)'")

    def test_cmake_script_change_includes_oracle_and_local_targets(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(Path(tmp))
            data = self.map(repo, "src/lib/CMakeLists.txt")
            self.assertIn("fxlib", data["build_targets"])
            self.assertIn("test_build_wiring_drift", data["build_targets"])

    def test_unwired_path_is_reported_not_guessed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(Path(tmp))
            (repo / "docs").mkdir()
            (repo / "docs" / "note.md").write_text("x\n", encoding="utf-8")
            data = self.map(repo, "docs/note.md")
            self.assertEqual(data["build_targets"], [])
            self.assertEqual(data["unwired"], ["docs/note.md"])

    def test_oversized_script_reports_count_and_oracle_only(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(Path(tmp))
            many = "\n".join("add_library(many_t%d t%d.cpp)" % (i, i) for i in range(25))
            (repo / "src" / "lib" / "CMakeLists.txt").write_text(
                "add_library(fxlib a.cpp)\n" + many + "\n", encoding="utf-8")
            data = self.map(repo, "src/lib/CMakeLists.txt")
            self.assertEqual(data["build_targets"], ["test_build_wiring_drift"])
            self.assertEqual(data["oversized_scripts"], {"src/lib/CMakeLists.txt": 26})

    def test_path_spellings_normalize(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(Path(tmp))
            plain = self.map(repo, "tests/test_a.cpp")
            dotted = self.map(repo, "./tests/test_a.cpp")
            self.assertEqual(plain["build_targets"], dotted["build_targets"])

    def test_path_outside_repo_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(Path(tmp))
            proc = run_tool("narrow_targets.py", ["/etc/passwd", "--json"], cwd=repo)
            self.assertEqual(proc.returncode, 2)
            proc = run_tool("narrow_targets.py", ["../outside.cpp", "--json"], cwd=repo)
            self.assertEqual(proc.returncode, 2)

    def test_variable_source_list_maps_strongly(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(Path(tmp))
            (repo / "src" / "lib" / "CMakeLists.txt").write_text(
                "set(FX_SRCS a.cpp)\n"
                "list(APPEND FX_SRCS b.cpp)\n"
                "add_library(fxlib ${FX_SRCS})\n", encoding="utf-8")
            data = self.map(repo, "src/lib/b.cpp")
            self.assertEqual(data["build_targets"], ["fxlib"])
            self.assertFalse(data["target_details"]["fxlib"]["weak"])

    def test_weak_basename_fallback_for_unresolvable_paths(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(Path(tmp))
            (repo / "src" / "lib" / "gen.cpp").write_text("int g;\n", encoding="utf-8")
            (repo / "src" / "lib" / "CMakeLists.txt").write_text(
                "add_library(fxlib a.cpp)\n"
                "target_sources(fxlib PRIVATE ${UNKNOWN_DIR}/gen.cpp)\n", encoding="utf-8")
            data = self.map(repo, "src/lib/gen.cpp")
            # the path is only known by basename: weak mapping, not unwired
            self.assertEqual(data["build_targets"], ["fxlib"])
            self.assertTrue(data["target_details"]["fxlib"]["weak"])

    def test_no_paths_reports_empty_payload(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(Path(tmp))
            proc = run_tool("narrow_targets.py", [], cwd=repo)
            self.assertEqual(proc.returncode, 2, proc.stderr)


if __name__ == "__main__":
    unittest.main()


class LinkGraphTest(unittest.TestCase):
    """The link graph turns a changed library source into its consumers:
    executables and test targets that (transitively) link the changed code
    get compile/link/test classification instead of being invisible."""

    def map(self, repo: Path, *paths: str) -> dict:
        proc = run_tool("narrow_targets.py", [*paths, "--json"], cwd=repo)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        return json.loads(proc.stdout)

    def make_link_fixture(self, root: Path) -> Path:
        (root / "src" / "lib").mkdir(parents=True)
        (root / "tests").mkdir(parents=True)
        proc = git(root, "init", "--quiet")
        assert proc.returncode == 0, proc.stderr
        (root / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\nproject(fx CXX)\n"
            "add_subdirectory(src/lib)\nadd_subdirectory(tests)\n", encoding="utf-8")
        (root / "src" / "lib" / "CMakeLists.txt").write_text(
            "add_library(fxcore a.cpp)\n"
            "add_library(Sicnu::fxcore ALIAS fxcore)\n"
            "add_library(fxmid b.cpp)\n"
            "target_link_libraries(fxmid PUBLIC Sicnu::fxcore)\n", encoding="utf-8")
        for f in ("a", "b"):
            (root / "src" / "lib" / (f + ".cpp")).write_text(f"int {f};\n", encoding="utf-8")
        (root / "tests" / "CMakeLists.txt").write_text(
            "add_executable(test_uses_mid test_uses_mid.cpp)\n"
            "target_link_libraries(test_uses_mid PRIVATE fxmid)\n"
            "add_executable(test_unrelated test_unrelated.cpp)\n", encoding="utf-8")
        for f in ("test_uses_mid", "test_unrelated"):
            (root / "tests" / (f + ".cpp")).write_text("int main(){return 0;}\n",
                                                       encoding="utf-8")
        return root

    def test_transitive_consumer_suggested_with_link_test(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = self.make_link_fixture(Path(tmp))
            data = self.map(repo, "src/lib/a.cpp")
            # a.cpp compiles into fxcore; test_uses_mid links fxmid→fxcore
            self.assertIn("fxcore", data["build_targets"])
            self.assertEqual(data["target_details"]["fxcore"]["kind"], "library")
            self.assertEqual(data["target_details"]["fxcore"]["verification"],
                             "compile+link")
            self.assertIn("test_uses_mid", data["build_targets"])
            self.assertEqual(data["target_details"]["test_uses_mid"]["verification"],
                             "link+test")
            self.assertIn("test_uses_mid", data["ctest_targets"])
            # the unrelated test is never suggested
            self.assertNotIn("test_unrelated", data["build_targets"])

    def test_alias_link_resolves_to_real_target(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = self.make_link_fixture(Path(tmp))
            data = self.map(repo, "src/lib/b.cpp")
            self.assertIn("fxmid", data["build_targets"])
            self.assertIn("test_uses_mid", data["ctest_targets"])

    def test_helper_kwarg_embeds_are_visible(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = self.make_link_fixture(Path(tmp))
            (root := Path(repo))
            (root / "tests" / "CMakeLists.txt").write_text(
                "function(sicnu_fx_d17_test NAME)\n"
                "  cmake_parse_arguments(D17 \"\" \"\" \"SOURCES;LIBS\" ${ARGN})\n"
                "  add_executable(${NAME} ${NAME}.cpp ${D17_SOURCES})\n"
                "endfunction()\n"
                "set(D17_ENGINE_SOURCES ${CMAKE_SOURCE_DIR}/src/lib/a.cpp)\n"
                "sicnu_fx_d17_test(test_d17_a\n"
                "  SOURCES ${D17_ENGINE_SOURCES}\n"
                "  LIBS Qt6::Core\n"
                ")\n", encoding="utf-8")
            data = self.map(repo, "src/lib/a.cpp")
            # the d17 embed target compiles a.cpp directly: it must appear
            self.assertIn("test_d17_a", data["build_targets"])
            self.assertEqual(data["target_details"]["test_d17_a"]["verification"],
                             "link+test")
