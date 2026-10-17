#!/usr/bin/env python3
"""check_translation_freshness.py — .ts freshness gate for #1489's residual.

Regenerates the zh_CN translation catalog with the SAME lupdate invocation
the build uses (the argument list is parsed out of the
`add_custom_target(sicnu_i18n_update ...)` command in the root CMakeLists.txt,
so the gate and the build target cannot drift) into a TEMPORARY file — the
tracked resources/translations/sicnu_zh_CN.ts is never rewritten — and then
compares, per (context, source) key:

  FAIL conditions (exit 1)
    - the tracked .ts carries more `type="unfinished"` messages than the
      tolerated baseline (--baseline-tolerance, default 0: a fully translated
      catalog is the steady state this repo is in);
    - the regenerated catalog contains a (context, source) message the
      tracked .ts does not have at all — i.e. a source string was added to
      the scanned directories without refreshing/Translating the .ts.

  WARN conditions (never fatal)
    - tracked entries that the regeneration dropped (stale/obsolete: removed
      sources; `-no-obsolete` deletes them on the next real update);
    - a message that regressed to unfinished in the regen while the tracked
      file holds a finished translation (lupdate preserves translations, so
      this would indicate toolchain oddity worth eyeballing).

`--selftest` constructs positive and negative fixtures in a temp directory
and runs the REAL lupdate + comparison against them.

Exit codes: 0 = fresh, 1 = gate failure, 2 = environment error (no lupdate,
unreadable inputs).
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

# Fallback ONLY for --dirs-less invocations outside a repo checkout. In the
# normal path the list is parsed from CMakeLists.txt's sicnu_i18n_update
# target; only the DIRECTORY list is parsed that way — the lupdate flags
# below in run_lupdate() are hardcoded to match that target and must be
# updated together if the target's flags ever change.
DEFAULT_LUPDATE_DIRS = [
    "src/app", "src/gui", "src/core", "src/processing", "src/analysis",
    "src/agent", "src/agent_ops", "src/workflow", "src/experiment_studio",
    "src/teaching_admin", "src/teaching", "src/operators", "src/ui",
    "src/python",
]

LUPDATE_CANDIDATES = [
    "/usr/lib/qt6/bin/lupdate",
    "/usr/lib/qt6/bin/lupdate-qt6",
]


def find_lupdate(explicit: str | None) -> str | None:
    if explicit:
        return explicit if Path(explicit).is_file() else None
    if os.environ.get("LUPDATE"):
        return os.environ["LUPDATE"]
    for cand in LUPDATE_CANDIDATES:
        if Path(cand).is_file():
            return cand
    return shutil.which("lupdate")


def lupdate_dirs_from_cmake(repo_root: Path) -> list[str] | None:
    """Extract the source-dir list from the sicnu_i18n_update target.

    The COMMAND line looks like:
      COMMAND Qt6::lupdate -no-obsolete -locations none src/app src/gui ...
              -ts resources/translations/sicnu_zh_CN.ts
    Tokens before `-ts` that are not flag-like are the scanned directories.
    """
    cmake = repo_root / "CMakeLists.txt"
    try:
        text = cmake.read_text(encoding="utf-8")
    except OSError:
        return None
    at = text.find("add_custom_target(sicnu_i18n_update")
    if at < 0:
        return None
    cmd_at = text.find("COMMAND", at)
    end_at = text.find(")", cmd_at) if cmd_at >= 0 else -1
    if cmd_at < 0 or end_at < 0:
        return None
    tokens = text[cmd_at:end_at].split()
    dirs = []
    skip = True  # skip the COMMAND + Qt6::lupdate tokens
    # flags that CONSUME a following value token (which must not be mistaken
    # for a scanned directory, e.g. "-locations none")
    valued_flags = {"-locations", "-encoding", "-source-language", "-target-language"}
    expect_value = False
    for tok in tokens[1:]:
        if tok == "-ts":
            break
        if expect_value:
            expect_value = False
            continue
        if tok in valued_flags:
            expect_value = True
            continue
        if tok.startswith("-"):
            continue
        if skip:
            skip = False  # the binary token (Qt6::lupdate)
            continue
        dirs.append(tok)
    return dirs or None


def parse_ts(path: Path) -> dict[tuple[str, str], dict]:
    """(context, source) -> {text, unfinished, vanished} for one .ts file."""
    root = ET.parse(path).getroot()
    out: dict[tuple[str, str], dict] = {}
    for context in root.findall("context"):
        name = (context.findtext("name") or "").strip()
        for message in context.findall("message"):
            source = message.findtext("source")
            if source is None:
                continue
            translation = message.find("translation")
            entry = {
                "text": (translation.text or "") if translation is not None else "",
                "unfinished": translation is not None
                and translation.get("type") == "unfinished",
                "vanished": translation is not None
                and translation.get("type") == "vanished",
            }
            out[(name, source)] = entry
    return out


def run_lupdate(lupdate: str, dirs: list[str], ts_out: Path, cwd: Path,
                extra_args: list[str] | None = None) -> None:
    # "-locations none" must reach lupdate as two argv entries — CMake splits
    # the COMMAND line the same way (the invocation this gate mirrors).
    cmd = [lupdate, "-no-obsolete", "-locations", "none", *(extra_args or []),
           *dirs, "-ts", str(ts_out)]
    proc = subprocess.run(cmd, cwd=str(cwd), capture_output=True, text=True, timeout=900)
    # lupdate exits 0 even with extraction warnings; a missing ts file is the
    # real failure signal.
    if not ts_out.is_file():
        raise RuntimeError(
            f"lupdate produced no output file (exit {proc.returncode}):\n"
            + (proc.stderr or "")[-2000:]
        )


def compare(tracked: dict, regen: dict, tolerance: int):
    failures: list[str] = []
    warnings: list[str] = []

    unfinished = sorted(k for k, v in tracked.items() if v["unfinished"])
    if len(unfinished) > tolerance:
        for ctx, src in unfinished[:20]:
            failures.append(
                f"unfinished in tracked .ts (baseline tolerance {tolerance}): "
                f"context='{ctx}' source='{src[:80]}'"
            )
        if len(unfinished) > 20:
            failures.append(f"... and {len(unfinished) - 20} more unfinished entries")

    new_messages = [k for k in regen if k not in tracked]
    for ctx, src in sorted(new_messages)[:20]:
        failures.append(
            f"new untranslated message (missing from tracked .ts): "
            f"context='{ctx}' source='{src[:80]}'"
        )
    if len(new_messages) > 20:
        failures.append(f"... and {len(new_messages) - 20} more new messages")

    regressed = [
        k for k, v in regen.items()
        if v["unfinished"] and k in tracked and not tracked[k]["unfinished"]
    ]
    for ctx, src in sorted(regressed)[:5]:
        warnings.append(
            f"regen marks unfinished but tracked has a translation "
            f"(toolchain oddity): context='{ctx}' source='{src[:80]}'"
        )

    stale = [k for k in tracked if k not in regen]
    if stale:
        warnings.append(
            f"{len(stale)} tracked entries not present in regeneration "
            f"(stale sources; -no-obsolete will drop them on the next update). "
            f"First few: " + "; ".join(f"{c}/{s[:40]}" for c, s in sorted(stale)[:5])
        )
    return failures, warnings


def gate(repo_root: Path, lupdate: str, ts_path: Path, dirs: list[str],
         tolerance: int) -> int:
    with tempfile.TemporaryDirectory(prefix="t09-ts-freshness-") as tmp:
        regen = Path(tmp) / "regen.ts"
        shutil.copyfile(ts_path, regen)  # lupdate merges into an existing .ts
        try:
            run_lupdate(lupdate, dirs, regen, repo_root)
        except (RuntimeError, subprocess.TimeoutExpired) as exc:
            print(f"ERROR: lupdate run failed: {exc}", file=sys.stderr)
            return 2
        tracked = parse_ts(ts_path)
        fresh = parse_ts(regen)

    print(f"tracked .ts : {len(tracked)} messages")
    print(f"regenerated : {len(fresh)} messages (dirs: {' '.join(dirs)})")
    failures, warnings = compare(tracked, fresh, tolerance)
    for w in warnings:
        print("WARN:", w)
    if failures:
        for f in failures:
            print("FAIL:", f)
        print(f"FRESHNESS GATE: FAIL ({len(failures)} problem(s))")
        return 1
    print("FRESHNESS GATE: PASS "
          f"(unfinished={sum(1 for v in tracked.values() if v['unfinished'])}<= {tolerance}, "
          f"new messages=0)")
    return 0


def selftest(lupdate: str) -> int:
    """Constructive pos/neg cases with a REAL lupdate run on a tiny fixture."""
    widget = (
        "#include <QWidget>\n"
        "class FakeWidget : public QWidget\n"
        "{\n"
        "    Q_OBJECT\n"
        "public:\n"
        "    void farewells();\n"
        "};\n"
        "void FakeWidget::farewells()\n"
        "{\n"
        "    tr(\"Hello\");\n"
        "    tr(\"Bye\");\n"
        "}\n"
    )
    ok = True
    with tempfile.TemporaryDirectory(prefix="t09-selftest-") as tmp:
        root = Path(tmp)
        src = root / "src" / "fake"
        src.mkdir(parents=True)
        (src / "widget.cpp").write_text(widget, encoding="utf-8")

        try:
            base = root / "base.ts"
            # -target-language: a .ts without a language attribute is never
            # updated by lupdate ("does not specify any target languages"),
            # so the fixture root must be created with one declared.
            run_lupdate(lupdate, ["src/fake"], base, root,
                        extra_args=["-source-language", "en", "-target-language", "zh_CN"])
        except (RuntimeError, subprocess.TimeoutExpired) as exc:
            print(f"SELFTEST: FAIL (lupdate could not process the fixture: {exc})")
            return 1
        # fill both messages with translations
        tree = ET.parse(base)
        for message in tree.getroot().iter("message"):
            tr = message.find("translation")
            if tr is not None:
                tr.attrib.pop("type", None)
                tr.text = "已翻译"
        complete = root / "complete.ts"
        tree.write(complete, encoding="utf-8", xml_declaration=True)

        # positive: tracked == regenerated-with-translations -> PASS
        regen = root / "pos_regen.ts"
        shutil.copyfile(complete, regen)
        run_lupdate(lupdate, ["src/fake"], regen, root)
        failures, _ = compare(parse_ts(complete), parse_ts(regen), 0)
        print("selftest positive (complete catalog):",
              "PASS" if not failures else f"FAIL {failures}")
        ok = ok and not failures

        # negative 1: a new source string the tracked .ts lacks
        (src / "widget.cpp").write_text(widget.replace(
            '    tr("Bye");\n', '    tr("Bye");\n    tr("New String");\n'), encoding="utf-8")
        regen2 = root / "neg_regen.ts"
        shutil.copyfile(complete, regen2)
        run_lupdate(lupdate, ["src/fake"], regen2, root)
        failures2, _ = compare(parse_ts(complete), parse_ts(regen2), 0)
        detected = any("new untranslated" in f for f in failures2)
        print("selftest negative (new message undetected=BAD):",
              "PASS (detected)" if detected else "FAIL (missed)")
        ok = ok and detected

        # negative 2: unfinished in the tracked file above tolerance
        tree2 = ET.parse(complete)
        for message in tree2.getroot().iter("message"):
            source = message.findtext("source")
            if source == "Hello":
                tr = message.find("translation")
                tr.set("type", "unfinished")
                tr.text = ""
        unfinished_ts = root / "unfinished.ts"
        tree2.write(unfinished_ts, encoding="utf-8", xml_declaration=True)
        regen3 = root / "neg3_regen.ts"
        shutil.copyfile(unfinished_ts, regen3)
        run_lupdate(lupdate, ["src/fake"], regen3, root)
        failures3, _ = compare(parse_ts(unfinished_ts), parse_ts(regen3), 0)
        detected3 = any("unfinished in tracked" in f for f in failures3)
        print("selftest negative (unfinished undetected=BAD):",
              "PASS (detected)" if detected3 else "FAIL (missed)")
        ok = ok and detected3

    print("SELFTEST:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo-root", default=None,
                    help="repository root (default: parent of this script's dir)")
    ap.add_argument("--lupdate", default=None, help="lupdate binary path")
    ap.add_argument("--ts", default=None,
                    help="tracked .ts (default: <root>/resources/translations/sicnu_zh_CN.ts)")
    ap.add_argument("--baseline-tolerance", type=int, default=0,
                    help="allowed number of unfinished entries in the tracked .ts")
    ap.add_argument("--dirs", nargs="+", default=None,
                    help="override the scanned source dirs (default: parsed "
                         "from the sicnu_i18n_update target in CMakeLists.txt)")
    ap.add_argument("--selftest", action="store_true",
                    help="run constructive positive/negative fixtures and exit")
    args = ap.parse_args()

    lupdate = find_lupdate(args.lupdate)
    if not lupdate:
        print("ERROR: no lupdate binary found (tried --lupdate, $LUPDATE, "
              + ", ".join(LUPDATE_CANDIDATES) + ", PATH)", file=sys.stderr)
        return 2

    if args.selftest:
        return selftest(lupdate)

    repo_root = Path(args.repo_root or Path(__file__).resolve().parent.parent)
    ts_path = Path(args.ts) if args.ts else (
        repo_root / "resources" / "translations" / "sicnu_zh_CN.ts")
    if not ts_path.is_file():
        print(f"ERROR: tracked .ts not found: {ts_path}", file=sys.stderr)
        return 2

    if args.dirs:
        dirs = args.dirs
    else:
        dirs = lupdate_dirs_from_cmake(repo_root)
        if dirs is None:
            # Fail loud (review R1 P2): silently falling back to
            # DEFAULT_LUPDATE_DIRS could let a reworded sicnu_i18n_update
            # target drift out of scope and the gate report a false PASS on
            # a stale directory list.
            print("ERROR: cannot parse the sicnu_i18n_update COMMAND line in "
                  f"{repo_root / 'CMakeLists.txt'} — refusing to guess the "
                  "lupdate scope; fix the parser or pass --dirs",
                  file=sys.stderr)
            return 2
    missing = [d for d in dirs if not (repo_root / d).is_dir()]
    if missing:
        print(f"ERROR: scanned directories missing under {repo_root}: {missing}",
              file=sys.stderr)
        return 2
    print(f"lupdate dirs (from CMakeLists sicnu_i18n_update): {' '.join(dirs)}")

    return gate(repo_root, lupdate, ts_path, dirs, args.baseline_tolerance)


if __name__ == "__main__":
    sys.exit(main())
