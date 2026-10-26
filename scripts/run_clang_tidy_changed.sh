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
if ! git rev-parse --verify --quiet "${BASE_REF}" >/dev/null; then
  # Fail closed: diffing an unresolvable ref would print "nothing to lint"
  # and exit 0 — a silent pass on a gate that is supposed to block (#1495).
  echo "[clang-tidy-lane] ERROR: neither the requested base nor HEAD~1 resolves; refusing to silently pass" >&2
  exit 2
fi

# #1683: diff from the MERGE-BASE, not the base ref's tip. Diffing vs
# origin/master's tip makes every agent/** push that is behind master lint
# OTHER people's master changes too, and a master push diffs to itself and
# no-ops. The merge-base is exactly "what this branch changed". On shallow
# checkouts where the common ancestor is not fetched, merge-base fails and we
# fall back to the old tip diff (loudly) — no worse than before.
MERGE_BASE="$(git merge-base HEAD "${BASE_REF}" 2>/dev/null || true)"
if [ -n "${MERGE_BASE}" ]; then
  DIFF_BASE="${MERGE_BASE}"
  BASE_MODE="merge-base ${MERGE_BASE} (own changes only)"
else
  DIFF_BASE="${BASE_REF}"
  BASE_MODE="${BASE_REF} tip (shallow history — merge-base unavailable; changes others landed on the base may be included)"
  echo "[clang-tidy-lane] WARNING: no merge-base with ${BASE_REF} — ${BASE_MODE}" >&2
fi
FILES="$(git diff --name-only --diff-filter=ACMR "${DIFF_BASE}" -- '*.cpp' '*.cc' '*.cxx' '*.h' '*.hpp')"
if [ -z "${FILES}" ]; then
  echo "[clang-tidy-lane] no changed C++ files vs ${BASE_MODE} — nothing to lint"
  exit 0
fi

echo "[clang-tidy-lane] linting changed files vs ${BASE_MODE}:"
echo "${FILES}"
# .clang-tidy's HeaderFilterRegex 'src/.*' matches ANY path containing
# "/src/" — including the build tree's fetched third parties
# (build*/_deps/catch2-src/src/...) and vendored refs/qgis/src/... With the
# gate blocking (#1495) that would fail PRs on Catch2's own macro debt, i.e.
# legacy flooding through the header path. #1683/#1696 follow-up: an
# unchanged-headers loophole remained — any ^ROOT/src/ header included by a
# changed TU could block, contradicting the lane's own contract ("lints ONLY
# files changed ... legacy backlog outside the diff cannot flood the gate",
# see header). The filter is now built from the CHANGED header files
# themselves: main-file diagnostics (the changed .cpp/.h files) are always
# reported regardless; changed headers stay linted; unchanged legacy headers
# cannot block a diff that merely includes them.
# Anchor on BOTH the logical and the physical checkout root: clang's
# diagnostic paths follow the include-resolution spelling, which in a
# symlinked checkout may be either (review P2-4). The escape class covers
# every ERE metacharacter so a path with + ? ( ) | { } cannot invalidate the
# regex.
escape_ere() { printf '%s' "$1" | sed 's/[][\\.^$*+?(){}|]/\\&/g'; }
root_alternation() {
  ROOT_LOGICAL="$(pwd)"
  ROOT_PHYSICAL="$(pwd -P)"
  if [ "${ROOT_LOGICAL}" = "${ROOT_PHYSICAL}" ]; then
    printf '%s' "$(escape_ere "${ROOT_LOGICAL}")"
  else
    printf '(%s|%s)' "$(escape_ere "${ROOT_LOGICAL}")" "$(escape_ere "${ROOT_PHYSICAL}")"
  fi
}
CHANGED_HEADERS="$(printf '%s\n' "${FILES}" | grep -E '\.(h|hpp)$' || true)"
if [ -n "${CHANGED_HEADERS}" ]; then
  ROOT_RE="$(root_alternation)"
  HEADER_FILTER="^${ROOT_RE}/($(printf '%s\n' "${CHANGED_HEADERS}" | while IFS= read -r f; do escape_ere "${f}"; done | paste -sd '|' -))$"
  echo "[clang-tidy-lane] header-filter: changed headers only ($(printf '%s\n' "${CHANGED_HEADERS}" | wc -l | tr -d ' ') entries; anchor follows compile_commands' absolute include spelling)" >&2
else
  # No changed headers: impossible filter, header diagnostics cannot fire.
  HEADER_FILTER='^$'
  echo "[clang-tidy-lane] header-filter: none (no changed .h/.hpp — anchor is a full abs path per compile_commands; if a future generator emits relative -I spellings this filter will silently match nothing)" >&2
fi
# shellcheck disable=SC2086 — filenames from git cannot contain spaces here
# (repo convention); xargs keeps the invocation one process per batch.
echo "${FILES}" | xargs clang-tidy -p "${BUILD_DIR}" --warnings-as-errors='*' \
  --header-filter="${HEADER_FILTER}"
