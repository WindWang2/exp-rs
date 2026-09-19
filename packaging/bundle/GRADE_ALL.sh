#!/bin/sh
# grade_all.sh — batch-grade a folder of student submissions offline (D7 /
# lab platform 12.0). POSIX twin of GRADE_ALL.cmd: same arguments, same
# environment, same exit codes, no interactive pause.
#
# Usage (double-click is a Windows habit; run it by path from anywhere):
#   /path/to/bundle/GRADE_ALL.sh <submissions_dir> [lab_id] [out.csv]
#     submissions_dir  one artifact file per student; student_id = file name
#                      without extension (e.g. 20240101.tif).
#     lab_id           grading rules id (default: ndvi_basics).
#     out.csv          result CSV (default: grades.csv next to the
#                      submissions folder), UTF-8 with BOM + CRLF so Excel
#                      opens Chinese correctly.
#
# The in-bundle CLI streams one submission at a time: a corrupt or unreadable
# file is recorded in the CSV and does NOT abort the remaining submissions.
# Exit codes: 0 all graded (individual verdicts may be "fail") | 1 isolated
# error rows / capped | 2 usage. Set SICNU_BATCH_FLAGS to pass extra flags to
# `lab --batch` (e.g. SICNU_BATCH_FLAGS="--json --html").
#
# Requires python3 only if the invoked lab needs it; this wrapper itself is
# POSIX sh with no dependencies beyond the bundle layout.

set -eu

# The script ships at the bundle root, next to GRADE_ALL.cmd / VERIFY.sh.
bundle_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ "$#" -lt 1 ]; then
  echo "usage: GRADE_ALL.sh <submissions_dir> [lab_id] [out.csv]" >&2
  echo "  example: GRADE_ALL.sh /media/usb/lab1_submissions ndvi_basics grades.csv" >&2
  exit 2
fi

subs=$1
lab=${2:-ndvi_basics}
csv=${3:-"$subs/grades.csv"}

# Anchor the submission directory before cd so relative caller paths survive.
case "$subs" in
  /*) ;;
  *) subs=$(CDPATH= cd -- "$(dirname -- "$subs")" && pwd)/$(basename -- "$subs") ;;
esac
case "$csv" in
  /*) ;;
  *) csv="$PWD/$csv" ;;
esac

if [ ! -d "$subs" ]; then
  echo "grade_all: submissions folder not found: $subs" >&2
  exit 2
fi

# A teacher pointing --csv at a fresh directory should not have to mkdir it.
csv_dir=$(dirname -- "$csv")
if [ ! -d "$csv_dir" ]; then
  mkdir -p -- "$csv_dir" || exit 2
fi

cli="$bundle_root/bin/sicnu_geo_rs_cli"
if [ ! -x "$cli" ]; then
  echo "grade_all: $cli missing - bundle incomplete." >&2
  exit 1
fi

SICNU_OFFLINE=1
export SICNU_OFFLINE
SICNU_LAB_RULES_DIR="$bundle_root/data/labs/grading"
export SICNU_LAB_RULES_DIR
PROJ_DATA="$bundle_root/data/runtime/proj"
export PROJ_DATA
GDAL_DATA="$bundle_root/data/runtime/gdal"
export GDAL_DATA
if [ -z "${QT_QPA_PLATFORM:-}" ]; then
  QT_QPA_PLATFORM=offscreen
  export QT_QPA_PLATFORM
fi

# shellcheck disable=SC2086  # SICNU_BATCH_FLAGS is intentionally word-split
set +e
"$cli" --offline lab --lab "$lab" --batch "$subs" --csv "$csv" ${SICNU_BATCH_FLAGS:-}
rc=$?
set -e

if [ "$rc" -eq 0 ]; then
  echo "Done. All submissions graded. CSV: $csv"
else
  echo "Finished with isolated error rows (exit $rc) - they are kept in the CSV." >&2
fi
exit "$rc"
