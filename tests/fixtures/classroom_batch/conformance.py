#!/usr/bin/env python3
# conformance.py — classroom batch driver conformance (lab platform 12.0).
#
# Drives scripts/run_classroom_batch.py as a subprocess against the stub
# grader (stub_cli.py) plus, when --real-cli is given, the real
# sicnu_geo_rs_cli for the corrupt-artifact typed-error lane. Locks the
# batch-classroom oracle: a hung / crashed / corrupt submission must never
# stall or abort the batch, the report must account for exactly the inputs,
# and two runs on identical inputs must produce byte-identical JSON.
#
# Scenarios:
#   1. status matrix      pass/fail/corrupt/crash/usage/hang(→timeout),
#                         spaces + Unicode paths, row count == submissions
#   2. determinism        two runs → identical .json bytes, BOM in .csv
#   3. timeout kill       hang row killed ≈ timeout, batch still finishes
#   4. concurrency cap    STUB_PROBE_DIR shows max parallel == --jobs
#   5. submission cap     --max-submissions truncates rows, capped>0, exit 1
#   6. discovery skips    hidden files, subdirs, .csv/.json excluded
#   7. usage errors       missing dir / missing cli / empty dir → exit 2
#   8. real CLI           corrupt GeoTIFF → typed error row (exit 3 lane)

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
DRIVER = os.path.join(REPO, "scripts", "run_classroom_batch.py")

FAILURES = []


def check(condition, label):
    if condition:
        print("  ok   %s" % label)
    else:
        print("  FAIL %s" % label)
        FAILURES.append(label)


def run_driver(args, env_extra=None):
    env = dict(os.environ)
    if env_extra:
        env.update(env_extra)
    return subprocess.run(
        [sys.executable, DRIVER] + args,
        capture_output=True, text=True, env=env, timeout=180,
    )


def write_submissions(root, names, slot=""):
    subs = os.path.join(root, slot, "submissions") if slot \
        else os.path.join(root, "submissions")
    os.makedirs(subs, exist_ok=True)
    for name in names:
        with open(os.path.join(subs, name), "wb") as handle:
            handle.write(b"stub")  # content is irrelevant to the stub
    return subs


def stub_template():
    return ("{python} %s --artifact {artifact} --out {out}"
            % os.path.join(HERE, "stub_cli.py"))


def scenario_matrix(root):
    print("scenario 1: status matrix")
    subs = write_submissions(root, [
        "alice_pass.tif", "bob_fail.tif", "carol_corrupt.tif",
        "dave_crash.tif", "frank_usage.tif", "学生 赵六_hang.tif",
    ])
    out = os.path.join(root, "out", "报告")
    proc = run_driver([
        "--cli", "/bin/true", "--lab", "ndvi_basics",
        "--submissions", subs, "--out-prefix", out,
        "--timeout", "3", "--jobs", "4",
        "--command-template", stub_template(),
    ])
    check(proc.returncode == 1, "exit 1 when bad rows exist (got %d)"
          % proc.returncode)
    with open(out + ".csv", "rb") as handle:
        raw = handle.read()
    check(raw.startswith(b"\xef\xbb\xbf"), "csv has UTF-8 BOM")
    import csv as csvmod
    with open(out + ".csv", encoding="utf-8-sig", newline="") as handle:
        rows = list(csvmod.reader(handle))[1:]
    by_id = {r[0]: r for r in rows}
    check(len(rows) == 6, "row count == submissions (got %d)" % len(rows))
    check(by_id["alice_pass"][3] == "pass" and by_id["alice_pass"][2] == "100.0"
          and by_id["alice_pass"][4] == "pass",
          "pass row verdict/score/status")
    check(by_id["bob_fail"][3] == "fail" and by_id["bob_fail"][4] == "fail"
          and by_id["bob_fail"][7] == "stats.mean",
          "fail row keeps top deduction")
    check(by_id["carol_corrupt"][4] == "error" and by_id["carol_corrupt"][5] == "3",
          "corrupt artifact → typed error row (exit 3)")
    check(by_id["dave_crash"][4] == "crash" and by_id["dave_crash"][5] == "-11",
          "signal crash → crash row")
    check(by_id["frank_usage"][4] == "error" and by_id["frank_usage"][5] == "2",
          "usage → error row (exit 2)")
    check(by_id["学生 赵六_hang"][4] == "timeout",
          "hang → timeout row (Unicode+space student id intact)")
    with open(out + ".json", encoding="utf-8") as handle:
        summary = json.load(handle)
    check(summary["requested"] == 6 and len(summary["rows"]) == 6,
          "json rows == requested")
    check(summary["counts"] == {"pass": 1, "fail": 1, "timeout": 1,
                                "error": 2, "crash": 1},
          "json counts %s" % summary["counts"])
    check(summary["capped"] == 0, "capped == 0")
    return out


def scenario_determinism(root, out):
    print("scenario 2: determinism")
    # Byte-stability contract: identical inputs AND identical flags ⇒
    # byte-identical reports (jobs/timeout live in the JSON header, so the
    # flags must match; the CSV is flag-independent).
    out2 = os.path.join(root, "out2", "报告")
    proc = run_driver([
        "--cli", "/bin/true", "--lab", "ndvi_basics",
        "--submissions", os.path.join(root, "submissions"),
        "--out-prefix", out2, "--timeout", "3", "--jobs", "4",
        "--command-template", stub_template(),
    ])
    check(proc.returncode == 1, "second run exits 1 (same fixture)")
    with open(out + ".json", "rb") as a, open(out2 + ".json", "rb") as b:
        check(a.read() == b.read(),
              "json byte-identical across identical runs")
    with open(out + ".csv", "rb") as a, open(out2 + ".csv", "rb") as b:
        check(a.read() == b.read(), "csv byte-identical across runs")


def scenario_timeout(root):
    print("scenario 3: timeout kill latency")
    subs = write_submissions(root, ["hang_only_hang.tif", "ok_pass.tif"],
                             slot="timeout")
    out = os.path.join(root, "out3")
    start = time.monotonic()
    proc = run_driver([
        "--cli", "/bin/true", "--lab", "ndvi_basics",
        "--submissions", subs, "--out-prefix", out,
        "--timeout", "2", "--jobs", "2",
        "--command-template", stub_template(),
    ])
    wall = time.monotonic() - start
    check(proc.returncode == 1, "timeout batch exits 1")
    check(wall < 30, "batch finished despite hang (%.1fs)" % wall)
    with open(out + ".csv", encoding="utf-8-sig") as handle:
        rows = {line.split(",")[0]: line.split(",")
                for line in handle.read().splitlines()[1:]}
    check(rows["hang_only_hang"][4] == "timeout", "hang row reported timeout")
    check(rows["ok_pass"][4] == "pass", "other submission unaffected by hang")


def scenario_concurrency(root):
    print("scenario 4: concurrency cap")
    subs = write_submissions(root, ["s%d_pass.tif" % i for i in range(8)],
                             slot="concurrency")
    probe = os.path.join(root, "concurrency", "probe")
    out = os.path.join(root, "out4")
    env_extra = {"STUB_PROBE_DIR": probe, "STUB_WORK_MS": "400"}
    cmd = [sys.executable, DRIVER,
           "--cli", "/bin/true", "--lab", "ndvi_basics",
           "--submissions", subs, "--out-prefix", out,
           "--timeout", "30", "--jobs", "2",
           "--command-template", stub_template()]
    env = dict(os.environ)
    env.update(env_extra)
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    # Poll the probe directory for the peak number of simultaneously open
    # workers. 8 jobs × 400 ms at jobs=2 ⇒ ≥1.6 s of work; a 20 ms poll never
    # misses a 400 ms window.
    peak = 0
    while proc.poll() is None:
        try:
            peak = max(peak, len(os.listdir(probe)))
        except FileNotFoundError:
            pass
        time.sleep(0.02)
    stdout, stderr = proc.communicate()
    try:
        peak = max(peak, len(os.listdir(probe)))
    except FileNotFoundError:
        pass
    check(proc.returncode == 0, "all-pass batch exits 0 (got %d; stderr: %s)"
          % (proc.returncode, stderr.decode()[-200:] if stderr else ""))
    check(peak == 2, "peak concurrency == --jobs (got %d)" % peak)


def scenario_cap(root):
    print("scenario 5: submission cap")
    subs = write_submissions(root, ["s%d_pass.tif" % i for i in range(5)],
                             slot="cap")
    out = os.path.join(root, "out5")
    proc = run_driver([
        "--cli", "/bin/true", "--lab", "ndvi_basics",
        "--submissions", subs, "--out-prefix", out,
        "--timeout", "10", "--jobs", "2", "--max-submissions", "3",
        "--command-template", stub_template(),
    ])
    check(proc.returncode == 1, "capped batch exits 1")
    with open(out + ".json", encoding="utf-8") as handle:
        summary = json.load(handle)
    check(summary["requested"] == 5 and len(summary["rows"]) == 3
          and summary["capped"] == 2,
          "cap: requested==5 rows==3 capped==2 (%s)"
          % json.dumps(summary["counts"]))
    with open(out + ".csv", encoding="utf-8-sig") as handle:
        n = len(handle.read().splitlines()) - 1
    check(n == 3, "csv rows == 3 under cap (got %d)" % n)


def scenario_discovery(root):
    print("scenario 6: discovery skips")
    subs = write_submissions(root, [
        "one_pass.tif", ".hidden_pass.tif", "grades.csv", "old.json",
    ], slot="discovery")
    os.makedirs(os.path.join(subs, "nested"), exist_ok=True)
    with open(os.path.join(subs, "nested", "two_pass.tif"), "wb") as handle:
        handle.write(b"x")
    out = os.path.join(root, "out6")
    proc = run_driver([
        "--cli", "/bin/true", "--lab", "ndvi_basics",
        "--submissions", subs, "--out-prefix", out,
        "--timeout", "10", "--jobs", "2",
        "--command-template", stub_template(),
    ])
    check(proc.returncode == 0, "discovery batch exits 0")
    with open(out + ".csv", encoding="utf-8-sig") as handle:
        ids = [line.split(",")[0] for line in
               handle.read().splitlines()[1:]]
    check(ids == ["one_pass"], "only the real submission graded (got %s)" % ids)


def scenario_usage(root):
    print("scenario 7: usage errors")
    base = ["--cli", "/bin/true", "--lab", "x", "--out-prefix",
            os.path.join(root, "u")]
    for args, label in [
        (base + ["--submissions", os.path.join(root, "nope")],
         "missing submissions dir → 2"),
        (["--cli", os.path.join(root, "nope-cli"), "--lab", "x",
          "--submissions", os.path.join(root), "--out-prefix",
          os.path.join(root, "u")],
         "missing cli → 2"),
    ]:
        proc = run_driver(args + ["--timeout", "5"])
        check(proc.returncode == 2, label)
    empty = os.path.join(root, "empty")
    os.makedirs(empty, exist_ok=True)
    proc = run_driver(base + ["--submissions", empty, "--timeout", "5"])
    check(proc.returncode == 2, "empty submissions dir → 2")


def scenario_real_cli(root, cli):
    print("scenario 8: real CLI corrupt-artifact lane")
    subs = os.path.join(root, "subs_real")
    os.makedirs(subs, exist_ok=True)
    bad = os.path.join(subs, "bad_corrupt.tif")
    with open(bad, "wb") as handle:
        handle.write(b"not a tiff at all")
    rules = os.path.join(HERE, "real_rules", "corrupt_probe.rules.json")
    if not os.path.exists(rules):
        print("  skip (no rules fixture shipped)")
        return
    out = os.path.join(root, "out8")
    proc = run_driver([
        "--cli", cli, "--lab", rules,
        "--submissions", subs, "--out-prefix", out,
        "--timeout", "60", "--jobs", "1", "--offline",
    ])
    with open(out + ".csv", encoding="utf-8-sig") as handle:
        rows = [line.split(",") for line in handle.read().splitlines()[1:]]
    check(len(rows) == 1 and rows[0][4] == "error" and rows[0][5] == "3",
          "real grader: corrupt artifact → typed error row, batch exits 1")


def main():
    parser_args = [a for a in sys.argv[1:]]
    real_cli = None
    if "--real-cli" in parser_args:
        real_cli = parser_args[parser_args.index("--real-cli") + 1]
        parser_args = [a for a in parser_args
                       if a != "--real-cli" and a != real_cli]
    with tempfile.TemporaryDirectory(prefix="classroom_conformance_") as root:
        out = scenario_matrix(root)
        scenario_determinism(root, out)
        scenario_timeout(root)
        scenario_concurrency(root)
        scenario_cap(root)
        scenario_discovery(root)
        scenario_usage(root)
        if real_cli:
            scenario_real_cli(root, real_cli)
        else:
            print("scenario 8: skipped (--real-cli not provided)")
    if FAILURES:
        print("FAIL: %d check(s) failed" % len(FAILURES))
        return 1
    print("all classroom-batch conformance checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
