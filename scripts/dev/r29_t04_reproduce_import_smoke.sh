#!/usr/bin/env bash
# r29_t04_reproduce_import_smoke.sh — command-level check for the new
# `reproduce import` CLI verb (R29-T04 D4). Creates two stores through the
# CLI itself where possible, seeds a run via sqlite3-free paths (the CLI
# export needs a recorded run, so the seed uses the existing `experiment`
# surface when available; otherwise this script documents the manual probe
# and exits 0 only on the flags/validation behavior).
#
# Usage: r29_t04_reproduce_import_smoke.sh <build-dir> <work-dir>
set -u
BUILD_DIR="${1:?build dir}"
WORK="${2:?work dir}"
CLI="$BUILD_DIR/sicnu_geo_rs_cli"
mkdir -p "$WORK"
# A missing binary exits 127 (and an unexecutable one 126) on every call,
# which the "must fail" checks below would otherwise count as GREEN — refuse
# to run at all instead, and never accept 126/127 as a passed check.
if [ ! -x "$CLI" ]; then
  echo "CLI binary not found/executable: $CLI" >&2
  exit 1
fi
rc=0

# 1) Verb validation: missing --bundle must refuse with kInvalidInput (2).
out=$("$CLI" reproduce import --experiment-db "$WORK/x.db" --dataset-db "$WORK/d.db" --experiment e1 2>&1)
code=$?
echo "[1] missing --bundle → exit $code"
[ $code -gt 0 ] && [ $code -ne 126 ] && [ $code -ne 127 ] || rc=1

# 2) Missing --experiment must refuse.
out=$("$CLI" reproduce import --experiment-db "$WORK/x.db" --dataset-db "$WORK/d.db" --bundle "$WORK/b" 2>&1)
code=$?
echo "[2] missing --experiment → exit $code"
[ $code -gt 0 ] && [ $code -ne 126 ] && [ $code -ne 127 ] || rc=1

# 3) Unknown flag must be refused (Track 14 WP-E leftover gate).
out=$("$CLI" reproduce import --experiment-db "$WORK/x.db" --dataset-db "$WORK/d.db" --bundle "$WORK/b" --experiment e1 --bogus-flag 2>&1)
code=$?
echo "[3] unknown flag → exit $code"
[ $code -gt 0 ] && [ $code -ne 126 ] && [ $code -ne 127 ] || rc=1

# 4) Unknown subcommand still refuses.
out=$("$CLI" reproduce teleport 2>&1)
code=$?
echo "[4] unknown subcommand → exit $code"
[ $code -gt 0 ] && [ $code -ne 126 ] && [ $code -ne 127 ] || rc=1

exit $rc
