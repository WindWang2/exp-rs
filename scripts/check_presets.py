#!/usr/bin/env python3
"""check_presets.py — CMakePresets.json hygiene assertions (WP2, ds41-build-portability).

Asserts, without running a configure:
  * schema basics (version 3, cmake minimum, names unique);
  * every build/test preset references an existing configure preset;
  * build presets pin a job count within the project's parallel cap
    (default 1, cap 2 — ADR 0147 / D7 resource discipline);
  * test presets pin -j1 and QT_QPA_PLATFORM=offscreen;
  * a dev, a CI and an offline-lab profile exist and are distinguishable;
  * no preset hardwires an absolute path in binaryDir (must be ${sourceDir}-relative)
    so the file is usable from a fresh clone on any machine.

Exit 0 iff every assertion holds; each check prints ok/FAIL with its evidence.
"""
from __future__ import annotations

import json
import sys

JOBS_CAP = 2  # hard cap; -j3+ is refused everywhere in this repo


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else "CMakePresets.json"
    try:
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"check_presets: FAIL: cannot read {path}: {exc}")
        return 1

    failures = 0

    def check(ok: bool, what: str, detail: str = "") -> None:
        nonlocal failures
        if ok:
            print(f"  ok: {what}")
        else:
            failures += 1
            print(f"  FAIL: {what}" + (f" — {detail}" if detail else ""))

    print(f"check_presets: {path}")

    check(data.get("version") == 3, "presets version is 3", str(data.get("version")))
    cmr = data.get("cmakeMinimumRequired", {})
    check(bool(cmr.get("major")), "cmakeMinimumRequired declared", str(cmr))

    conf = data.get("configurePresets", [])
    names = [p.get("name") for p in conf]
    check(len(names) == len(set(names)), "configure preset names unique", str(names))
    check(all(p.get("binaryDir") for p in conf), "every configure preset has binaryDir")
    check(
        all(p.get("description") or p.get("displayName") for p in conf),
        "every configure preset is described",
    )
    check(
        all(str(p.get("binaryDir", "")).startswith("${sourceDir}") for p in conf),
        "binaryDir is ${sourceDir}-relative (no absolute paths)",
    )

    # profile coverage: a dev lane, a CI lane, an offline/lab lane
    lab = [p for p in conf if str(p.get("cacheVariables", {}).get("SICNU_LAB_PROFILE")) == "ON"]
    check(len(lab) >= 1, "an offline/lab profile preset exists (SICNU_LAB_PROFILE=ON)")
    dev = [p for p in conf if str(p.get("cacheVariables", {}).get("CMAKE_BUILD_TYPE")) == "Debug"]
    check(len(dev) >= 1, "a Debug dev profile exists")
    rel = [p for p in conf if str(p.get("cacheVariables", {}).get("CMAKE_BUILD_TYPE")) == "Release"]
    check(len(rel) >= 1, "a Release CI profile exists")

    builds = data.get("buildPresets", [])
    check(len(builds) >= 1, "build presets exist")
    for bp in builds:
        ref = bp.get("configurePreset")
        check(ref in names, f"build preset '{bp.get('name')}' references an existing configure preset", str(ref))
        jobs = bp.get("jobs")
        check(
            isinstance(jobs, int) and 1 <= jobs <= JOBS_CAP,
            f"build preset '{bp.get('name')}' pins jobs within 1..{JOBS_CAP}",
            str(jobs),
        )

    tests = data.get("testPresets", [])
    check(len(tests) >= 1, "test presets exist")
    for tp in tests:
        ref = tp.get("configurePreset")
        check(ref in names, f"test preset '{tp.get('name')}' references an existing configure preset", str(ref))
        # CMake <3.28 has no testPreset "jobs" field; ctest parallelism is pinned
        # through the environment instead (CTEST_PARALLEL_LEVEL=1, offscreen Qt).
        env = tp.get("environment", {})
        check(
            str(env.get("CTEST_PARALLEL_LEVEL")) == "1",
            f"test preset '{tp.get('name')}' pins ctest -j1 (CTEST_PARALLEL_LEVEL)",
            str(env),
        )
        check(
            env.get("QT_QPA_PLATFORM") == "offscreen",
            f"test preset '{tp.get('name')}' runs offscreen",
            str(env),
        )
        out = tp.get("output", {})
        check(
            bool(out.get("outputOnFailure")),
            f"test preset '{tp.get('name')}' reports failures verbatim",
        )

    if failures == 0:
        print("check_presets: ALL PASS")
        return 0
    print(f"check_presets: FAILED ({failures})")
    return 1


if __name__ == "__main__":
    sys.exit(main())
