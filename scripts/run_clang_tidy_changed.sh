#!/usr/bin/env bash
# scripts/run_clang_tidy_changed.sh — changed-files clang-tidy lane (#706).
#
# The repo ships a targeted .clang-tidy (12 checks aimed at exactly the bug
# classes hand-fixed in #610-#658), but nothing ever ran it. This lane lints
# ONLY C/C++ files changed relative to a base ref, so a full-tree tidy run
# (hours on the QGIS-scale sources) is never needed.
#
# Usage: run_clang_tidy_changed.sh [base-ref] [build-dir]
#   base-ref   git ref to diff against (default: origin/master; falls back
#              to HEAD~1 when the ref does not exist locally)
#   build-dir  configured build tree supplying compile_commands.json
#              (default: build)
#
# The script degrades to a no-op (exit 0) when clang-tidy is missing or no
# compile_commands.json is available, so it is safe to wire as a CI step.
set -u

BASE_REF="${1:-origin/master}"
BUILD_DIR="${2:-build}"

cd "$(git rev-parse --show-toplevel)" || exit 1

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "[clang-tidy-lane] clang-tidy not found — skipping (apt install clang-tidy)" >&2
  exit 0
fi

if [ ! -f "${BUILD_DIR}/compile_commands.json" ]; then
  echo "[clang-tidy-lane] no ${BUILD_DIR}/compile_commands.json — skipping (configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)" >&2
  exit 0
fi

if ! git rev-parse --verify --quiet "${BASE_REF}" >/dev/null; then
  echo "[clang-tidy-lane] base ref '${BASE_REF}' not found locally — falling back to HEAD~1" >&2
  BASE_REF="HEAD~1"
fi

# Diff base..working tree (not merge-base): robust on shallow CI checkouts,
# and on master pushes the diff is empty so the lane no-ops.
FILES="$(git diff --name-only --diff-filter=ACMR "${BASE_REF}" -- '*.cpp' '*.cc' '*.cxx' '*.h' '*.hpp')"
if [ -z "${FILES}" ]; then
  echo "[clang-tidy-lane] no changed C++ files vs ${BASE_REF} — nothing to lint"
  exit 0
fi

echo "[clang-tidy-lane] linting changed files vs ${BASE_REF}:"
echo "${FILES}"
# shellcheck disable=SC2086 — filenames from git cannot contain spaces here
# (repo convention); xargs keeps the invocation one process per batch.
echo "${FILES}" | xargs clang-tidy -p "${BUILD_DIR}" --warnings-as-errors='*'
