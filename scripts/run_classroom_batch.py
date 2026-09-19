#!/usr/bin/env python3
# run_classroom_batch.py — process-isolated classroom batch grading (lab platform 12.0).
#
# Grades a directory of student submissions by running one
# `sicnu_geo_rs_cli lab --grade` subprocess per submission, so that a hung,
# crashed or corrupted submission can never stall or take down the rest of the
# class. Complements the in-process `lab --batch` runner (src/cli/lab_batch_runner.*),
# which is serial and shares the grader's address space: it isolates *rows*,
# not *processes*. This driver adds the three hardening properties the
# in-process runner cannot provide:
#
#   * per-submission wall-clock timeout — the worker's whole process group is
#     killed on expiry and the submission is reported as "timeout";
#   * process isolation — a crash (signal) in the grader is contained to one
#     row and reported as "crash" instead of aborting the batch;
#   * bounded concurrency — at most --jobs workers run at once (default
#     min(4, cpu_count)), regardless of class size.
#
# Reports (both are always written, atomically via temp+rename):
#   <out-prefix>.csv  student_id,lab_id,score,verdict,status,exit_code,
#                     artifact_path,top_deduction,message (UTF-8 BOM, CRLF,
#                     Excel-friendly — same convention as LabBatchRunner)
#   <out-prefix>.json deterministic `sicnu.classroom.batch/1` summary: counts
#                     plus rows sorted by student_id. Byte-stable across two
#                     runs on identical inputs: no wall clock, no durations
#                     (opt into both with --stamp / --include-timings).
#
# Exit codes: 0 every submission graded (verdicts may still be "fail");
#             1 at least one timeout/error/crash row or the cap hit;
#             2 usage error (bad arguments, missing dirs, missing CLI).
#
# Determinism note: "error" is *expected* for a corrupt artifact (the grader
# reports it typed, exit 3). A "crash" row (negative exit code / signal) is a
# bug by definition — the batch still completes, but the exit code flags it.
#
# Windows: terminate() is used instead of process-group kill; the TREE /F
# taskkill fallback is best-effort. Graded-by semantics are identical.

import argparse
import concurrent.futures
import csv
import io
import json
import os
import shlex
import signal
import subprocess
import sys
import tempfile
import time

SCHEMA = "sicnu.classroom.batch/1"
CSV_HEADER = [
    "student_id", "lab_id", "score", "verdict", "status", "exit_code",
    "artifact_path", "top_deduction", "message",
]
# Statuses that mean "the submission did not produce a trustworthy grade".
BAD_STATUSES = ("timeout", "error", "crash")
GRADED_STATUSES = ("pass", "fail")


def log(message):
    if not QUIET:
        sys.stderr.write("run_classroom_batch: %s\n" % message)
        sys.stderr.flush()


def eprint(message):
    sys.stderr.write("run_classroom_batch: %s\n" % message)
    sys.stderr.flush()


def build_command(template, python_exe, worker, artifact, transcript, extra_args):
    """Expand the command template. Placeholders: {python} {worker} {cli}
    {lab} {artifact} {out} {extra}. Without a template the real grader is
    invoked directly (canonical argv, matching the bundle INSTRUCTIONS)."""
    if not template:
        return (
            [CLI_PATH]
            + list(extra_args)
            + ["lab", "--lab", LAB_ID, "--grade", artifact, "--out", transcript]
        )
    return [
        part.replace("{python}", python_exe)
            .replace("{worker}", worker or "")
            .replace("{cli}", CLI_PATH)
            .replace("{lab}", LAB_ID)
            .replace("{artifact}", artifact)
            .replace("{out}", transcript)
            .replace("{extra}", " ".join(shlex.quote(a) for a in extra_args))
        for part in shlex.split(template)
    ]


def kill_process_group(proc):
    """Best-effort hard kill of the worker and everything it spawned."""
    if proc.poll() is not None:
        return
    if os.name == "nt":
        try:
            subprocess.run(
                ["taskkill", "/PID", str(proc.pid), "/T", "/F"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                timeout=10,
            )
        except (OSError, subprocess.SubprocessError):
            proc.kill()
        return
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
    except (ProcessLookupError, PermissionError, OSError):
        try:
            proc.kill()
        except OSError:
            pass


def run_one(args):
    """Grade one submission in a child process. Never raises."""
    (student_id, artifact), ctx = args
    start = time.monotonic()
    row = {
        "student_id": student_id,
        "lab_id": ctx["lab"],
        "score": None,
        "verdict": None,
        "status": "error",
        "exit_code": None,
        "artifact_path": artifact,
        "top_deduction": "",
        "message": "",
    }
    transcript = ""
    try:
        # Inside the try: an unwritable report directory is a typed error
        # row, never an exception escaping the worker's never-raise contract.
        fd, transcript = tempfile.mkstemp(prefix="grade_", suffix=".json",
                                          dir=ctx["tmp_dir"])
        os.close(fd)
        command = build_command(ctx["template"], sys.executable,
                                ctx["worker"], artifact, transcript,
                                ctx["extra_args"])
        proc = subprocess.Popen(
            command, cwd=ctx["cwd"],
            env=ctx["env"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            start_new_session=(os.name != "nt"),
        )
        try:
            out, err = proc.communicate(timeout=ctx["timeout"])
        except subprocess.TimeoutExpired:
            kill_process_group(proc)
            out, err = proc.communicate()
            row["status"] = "timeout"
            row["message"] = "killed after %ss wall time" % ctx["timeout"]
            return row, time.monotonic() - start
        code = proc.returncode
        row["exit_code"] = code
        if code == 0 or code == 1:
            # Graded (0=pass, 1=fail). Trust the transcript, not the exit code
            # alone: the transcript carries score, verdict and deductions.
            parsed = read_transcript(transcript, row)
            if parsed:
                row["verdict"] = parsed["verdict"]
                row["score"] = parsed["score"]
                row["status"] = parsed["verdict"] if parsed["verdict"] in GRADED_STATUSES else "error"
                row["top_deduction"] = parsed["top_deduction"]
                if parsed["verdict"] not in GRADED_STATUSES:
                    row["status"] = "error"
                    row["message"] = "transcript verdict %r" % parsed["verdict"]
            else:
                row["status"] = "error"
                row["message"] = "exit %s but transcript missing or unreadable" % code
        elif code == 2:
            row["status"] = "error"
            row["message"] = decode_tail(err) or "usage error (exit 2)"
        elif code == 3:
            row["status"] = "error"
            row["message"] = decode_tail(err) or "unverifiable artifact (exit 3)"
        else:
            # Negative on POSIX = killed by signal N. That is a grader bug;
            # contain it, flag the row, keep the batch alive.
            row["status"] = "crash"
            detail = decode_tail(err)
            row["message"] = (
                "signal %s" % (-code) if code is not None and code < 0
                else "unexpected exit %s" % code)
            if detail:
                row["message"] += ": " + detail
        return row, time.monotonic() - start
    except Exception as exc:  # noqa: BLE001 — one bad job must never abort the batch
        row["status"] = "error"
        row["message"] = "driver exception: %s" % exc
        return row, time.monotonic() - start
    finally:
        try:
            os.remove(transcript)
        except OSError:
            pass


def decode_tail(raw, limit=400):
    if not raw:
        return ""
    text = raw.decode("utf-8", "replace").strip()
    return text[-limit:]


def read_transcript(path, row):
    """Extract verdict/score/top deduction from a sicnu.lab.grade/1 transcript."""
    try:
        with open(path, "r", encoding="utf-8") as handle:
            doc = json.load(handle)
    except (OSError, ValueError):
        return None
    report = doc.get("report") if isinstance(doc, dict) else None
    if not isinstance(report, dict):
        return None
    verdict = report.get("verdict")
    if not isinstance(verdict, str):
        return None
    score = report.get("score")
    top = ""
    deductions = report.get("deductions")
    if isinstance(deductions, list) and deductions:
        best = None
        for item in deductions:
            if not isinstance(item, dict):
                continue
            weight = item.get("weight")
            try:
                weight_f = float(weight)
            except (TypeError, ValueError):
                continue
            if best is None or weight_f > best[0]:
                best = (weight_f, str(item.get("assertion_id", "")))
        if best:
            top = best[1]
    return {"verdict": verdict, "score": score, "top_deduction": top}


def discover_submissions(submissions_dir, out_prefix, pattern):
    """Sorted submission files. Skips hidden files, subdirectories, previous
    report targets and anything that is not a regular file."""
    skip_names = set()
    if out_prefix:
        base = os.path.basename(out_prefix)
        skip_names.update({base + ".csv", base + ".json"})
    found = []
    for name in sorted(os.listdir(submissions_dir)):
        path = os.path.join(submissions_dir, name)
        if name.startswith(".") or name in skip_names:
            continue
        if os.path.splitext(name)[1].lower() in (".csv", ".json"):
            continue
        if not os.path.isfile(path):
            continue
        if pattern and not _fnmatch(name, pattern):
            continue
        found.append((os.path.splitext(name)[0], path))
    return found


def _fnmatch(name, pattern):
    import fnmatch
    return fnmatch.fnmatch(name, pattern)


def csv_safe(cell):
    """Neutralise spreadsheet formula injection: a cell beginning with
    = + - @ (or TAB/CR) would execute as a formula when a teacher opens the
    CSV in Excel. Student ids come from filenames and messages from grader
    stderr — both are attacker-influenced in a classroom."""
    if isinstance(cell, str) and cell[:1] in ("=", "+", "-", "@", "\t", "\r"):
        return "'" + cell
    return cell


def write_csv(path, rows):
    buffer = io.StringIO()
    writer = csv.writer(buffer, lineterminator="\r\n")
    writer.writerow(CSV_HEADER)
    for row in rows:
        writer.writerow([
            csv_safe(row["student_id"]), csv_safe(row["lab_id"]),
            "" if row["score"] is None else row["score"],
            "" if row["verdict"] is None else row["verdict"],
            row["status"],
            "" if row["exit_code"] is None else row["exit_code"],
            csv_safe(row["artifact_path"]), csv_safe(row["top_deduction"]),
            csv_safe(row["message"]),
        ])
    atomic_write(path, "﻿" + buffer.getvalue())


def build_summary(rows, lab, total, args):
    counts = {"pass": 0, "fail": 0, "timeout": 0, "error": 0, "crash": 0}
    for row in rows:
        counts[row["status"]] = counts.get(row["status"], 0) + 1
    summary = {
        "schema": SCHEMA,
        "lab": lab,
        "submissions_dir": args.submissions,
        "requested": total,
        "graded": counts["pass"] + counts["fail"],
        "counts": counts,
        "capped": max(0, total - len(rows)),
        "timeout_seconds": args.timeout,
        "jobs": args.jobs,
        "rows": [
            {
                "student_id": r["student_id"],
                "score": r["score"],
                "verdict": r["verdict"],
                "status": r["status"],
                "exit_code": r["exit_code"],
                "artifact": os.path.basename(r["artifact_path"]),
                "top_deduction": r["top_deduction"],
                "message": r["message"],
            }
            for r in sorted(rows, key=lambda r: r["student_id"])
        ],
    }
    if args.stamp:
        summary["generated_utc"] = time.strftime(
            "%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    if args.include_timings:
        summary["duration_seconds"] = {
            r["student_id"]: round(d, 3)
            for r, d in sorted(RESULTS, key=lambda kv: kv[0][0])
        }
    return summary


RESULTS = []


def atomic_write(path, text):
    directory = os.path.dirname(os.path.abspath(path))
    os.makedirs(directory, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=".classroom_", suffix=".tmp",
                               dir=directory)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as handle:
            handle.write(text)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.remove(tmp)
        except OSError:
            pass
        raise


def main(argv=None):
    global QUIET, CLI_PATH, LAB_ID
    parser = argparse.ArgumentParser(
        description="Process-isolated classroom batch grading for SICNU labs.")
    parser.add_argument("--cli", required=True,
                        help="path to sicnu_geo_rs_cli")
    parser.add_argument("--lab", required=True,
                        help="lab id or .rules.json path")
    parser.add_argument("--submissions", required=True,
                        help="directory of submission artifacts")
    parser.add_argument("--out-prefix", required=True,
                        help="base path for <base>.csv and <base>.json")
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="per-submission wall seconds (default 120)")
    parser.add_argument("--jobs", type=int, default=None,
                        help="max concurrent workers (default min(4, cpus))")
    parser.add_argument("--max-submissions", type=int, default=None,
                        help="cap the number of graded submissions")
    parser.add_argument("--pattern", default="*",
                        help="submission filename glob (default *)")
    parser.add_argument("--offline", action="store_true",
                        help="pass --offline and set SICNU_OFFLINE=1")
    parser.add_argument("--env", action="append", default=[],
                        metavar="KEY=VALUE",
                        help="extra child env (repeatable)")
    parser.add_argument("--command-template", default=None,
                        help="override the worker command (testing seam; "
                             "shlex-split; {python} {worker} {cli} {lab} "
                             "{artifact} {out} {extra} placeholders)")
    parser.add_argument("--worker", default=None,
                        help="worker script for --command-template")
    parser.add_argument("--stamp", action="store_true",
                        help="add generated_utc to the JSON summary")
    parser.add_argument("--include-timings", action="store_true",
                        help="add per-row durations to the JSON summary")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)
    QUIET = args.quiet
    CLI_PATH = args.cli
    LAB_ID = args.lab

    if args.timeout <= 0:
        eprint("--timeout must be positive")
        return 2
    if args.jobs is None:
        args.jobs = max(1, min(4, os.cpu_count() or 1))
    if args.jobs < 1:
        eprint("--jobs must be >= 1")
        return 2
    if not os.path.isfile(args.cli):
        eprint("CLI not found: %s" % args.cli)
        return 2
    if not os.path.isdir(args.submissions):
        eprint("submissions directory not found: %s" % args.submissions)
        return 2
    # Children run with cwd inside the submissions directory; any worker path
    # must therefore be absolute to survive that chdir.
    # --lab may be an id ("ndvi_basics") or a rules path; paths are anchored
    # to the driver's cwd because children run inside the submissions dir.
    if "/" in args.lab or args.lab.endswith(".rules.json"):
        args.lab = os.path.abspath(args.lab)
    if args.worker:
        args.worker = os.path.abspath(args.worker)
    if args.cli:
        args.cli = os.path.abspath(args.cli)
    args.submissions = os.path.abspath(args.submissions)
    args.out_prefix = os.path.abspath(args.out_prefix)
    CLI_PATH = args.cli  # keep the module-level copies absolute as well
    LAB_ID = args.lab

    submissions = discover_submissions(args.submissions, args.out_prefix,
                                       args.pattern)
    if not submissions:
        eprint("no submissions found in %s (pattern %r)" % (args.submissions,
                                                            args.pattern))
        return 2
    total_found = len(submissions)
    capped = False
    if args.max_submissions is not None and args.max_submissions >= 0:
        if len(submissions) > args.max_submissions:
            capped = True
            submissions = submissions[:args.max_submissions]

    child_env = dict(os.environ)
    if args.offline:
        child_env["SICNU_OFFLINE"] = "1"
    for item in args.env:
        key, _, value = item.partition("=")
        if not key:
            eprint("bad --env entry %r (want KEY=VALUE)" % item)
            return 2
        child_env[key] = value
    cli_args = ["--offline"] if args.offline else []

    ctx = {
        "lab": args.lab,
        "timeout": args.timeout,
        "template": args.command_template,
        "worker": args.worker,
        "cwd": args.submissions,
        "env": child_env,
        "extra_args": cli_args,
        "tmp_dir": os.path.dirname(os.path.abspath(args.out_prefix)) or ".",
    }
    os.makedirs(ctx["tmp_dir"], exist_ok=True)
    del RESULTS[:]
    def flatten(row):
        # Message text goes into a single CSV cell; embedded newlines (the
        # CLI prints multi-line diagnostics) become "; " so naive parsers and
        # Excel row scanning stay sane.
        if "\n" in row["message"]:
            row["message"] = "; ".join(row["message"].splitlines())
        return row

    rows = []
    with concurrent.futures.ThreadPoolExecutor(
            max_workers=args.jobs) as pool:
        for row, duration in pool.map(run_one,
                                      [(s, ctx) for s in submissions]):
            rows.append(flatten(row))
            RESULTS.append(((row["student_id"], row["artifact_path"]),
                            duration))
            log("%-24s %-8s %s" % (row["student_id"], row["status"],
                                   row["message"]))
    counts = {}
    for row in rows:
        counts[row["status"]] = counts.get(row["status"], 0) + 1

    write_csv(args.out_prefix + ".csv", rows)
    summary = build_summary(rows, args.lab, total_found, args)
    atomic_write(args.out_prefix + ".json",
                 json.dumps(summary, ensure_ascii=False, indent=2,
                            sort_keys=True) + "\n")

    log("graded=%d pass=%d fail=%d timeout=%d error=%d crash=%d capped=%s"
        % (counts.get("pass", 0) + counts.get("fail", 0),
           counts.get("pass", 0), counts.get("fail", 0),
           counts.get("timeout", 0), counts.get("error", 0),
           counts.get("crash", 0), summary["capped"]))
    if len(rows) != len(submissions):
        eprint("row count %d != submission count %d — refusing to report "
               "success" % (len(rows), len(submissions)))
        return 1
    bad = sum(counts.get(s, 0) for s in BAD_STATUSES)
    if capped:
        return 1
    return 1 if bad else 0


QUIET = False
CLI_PATH = ""
LAB_ID = ""

if __name__ == "__main__":
    sys.exit(main())
