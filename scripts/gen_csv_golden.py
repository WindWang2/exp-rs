#!/usr/bin/env python3
# scripts/gen_csv_golden.py — the shared CSV formula-injection corpus.
#
# Two implementations of the same classroom-safety rule exist in this repo:
#
#   * scripts/run_classroom_batch.py::csv_safe()  — the Python batch driver
#   * src/cli/lab_batch_runner.cpp::csvSafeCell() — the in-process C++ runner
#
# They are separate code paths and would silently drift. This script turns the
# Python side into a golden vector file that the Catch2 lane
# tests/test_lab_batch_classroom_safety.cpp ("[csv][formula-injection][parity]")
# replays against the C++ side, so any divergence turns a gate red.
#
#   python3 scripts/gen_csv_golden.py            # write tests/data/csv_injection_golden.json
#   python3 scripts/gen_csv_golden.py --check    # exit 1 when the file is stale
#
# The corpus is deliberately larger than the six dangerous characters: it also
# pins the counter-examples (a '=' that is NOT the first character, a leading
# space, an empty cell), because over-neutralizing is its own bug — it would
# corrupt real student ids and real scores.

import argparse
import importlib.util
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "tests", "data", "csv_injection_golden.json")

# Realistic classroom payloads plus the counter-examples. Order is part of the
# contract: keep it stable, append only.
CORPUS = [
    # --- must be neutralized (leading character is a formula trigger) ---
    "=cmd|'/c calc'!A1",
    "=1+1",
    "+SUM(A1:A9)",
    "-2+3+cmd|'/c calc'!A0",
    "@SUM(1+9)*cmd",
    "=HYPERLINK(\"http://evil.example\",\"click\")",
    "\t=tab-led",
    "\r=cr-led",
    "-12.5",
    "+",
    "=",
    # --- must NOT be touched ---
    "",
    "2024001",
    "a=b",
    "张=三",
    "  =lead",
    "\n=x",
    "lab12_sar_processing",
    "student_name (late)",
]


def load_csv_safe():
    """Import the reference implementation from the batch driver.

    Imported rather than re-implemented: re-implementing it here would make
    this script the authority instead of the mirror, and the parity gate would
    then compare the C++ side against a copy that can drift unnoticed.
    """
    path = os.path.join(ROOT, "scripts", "run_classroom_batch.py")
    spec = importlib.util.spec_from_file_location("sicnu_run_classroom_batch", path)
    if spec is None or spec.loader is None:
        raise SystemExit("gen_csv_golden: cannot import %s" % path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.csv_safe


def golden_bytes():
    csv_safe = load_csv_safe()
    cases = []
    for value in CORPUS:
        out = csv_safe(value)
        if not isinstance(out, str):
            raise SystemExit("gen_csv_golden: csv_safe returned %r for %r" % (out, value))
        cases.append({"input": value, "output": out})
    doc = {
        "schema": "sicnu.csv-injection-golden/1",
        "source": "scripts/run_classroom_batch.py::csv_safe",
        "cases": cases,
    }
    return json.dumps(doc, ensure_ascii=False, indent=2) + "\n"


def main():
    parser = argparse.ArgumentParser(
        description="regenerate the shared CSV formula-injection golden corpus")
    parser.add_argument("--check", action="store_true",
                        help="exit 1 when the golden file is not byte-identical")
    args = parser.parse_args()

    text = golden_bytes()
    if args.check:
        if not os.path.isfile(OUT):
            print("gen_csv_golden: --check FAILED, missing %s" % OUT)
            return 1
        with open(OUT, "r", encoding="utf-8") as handle:
            if handle.read() != text:
                print("gen_csv_golden: --check FAILED, %s is stale" % OUT)
                return 1
        print("gen_csv_golden: --check ok (%d cases)" % len(CORPUS))
        return 0

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    print("wrote %s (%d cases)" % (OUT, len(CORPUS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
