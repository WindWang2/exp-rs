#!/usr/bin/env bash
# dep_fixture_test.sh — hermetic gate for the configure-time dependency doctor.
#
# Configures tests/fixtures/dep_doctor_fixture/ in three cases and asserts the
# doctor's contract (no compiler, no Qt, no network — only cmake itself):
#
#   missing  GDAL absent (CMAKE_DISABLE_FIND_PACKAGE_GDAL=ON)
#           -> configure FAILS with per-platform install guidance.
#   stale    FakeDep 1.0 present, minimum 2.0 requested
#           -> configure FAILS naming the minimum version.
#   ok       FakeDep 1.0 present, minimum 1.0 requested
#           -> configure SUCCEEDS and prints the dependency summary.
#
# Exit 0 only when all three assertions hold. Usage: scripts/dep_fixture_test.sh
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/.." && pwd)
fixture_dir="$repo_root/tests/fixtures/dep_doctor_fixture"

# Locate cmake: PATH first, then the well-known VS-bundled install (Windows
# developer machines commonly lack a PATH cmake — probe, never hardwire).
find_cmake() {
    if [ -n "${SICNU_CMAKE:-}" ]; then
        printf '%s\n' "$SICNU_CMAKE"; return 0
    fi
    if command -v cmake >/dev/null 2>&1; then
        command -v cmake; return 0
    fi
    for c in \
        "/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
        "/c/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
        "/c/Program Files/CMake/bin/cmake.exe" \
        "/usr/bin/cmake" "/usr/local/bin/cmake" "/opt/homebrew/bin/cmake"; do
        if [ -x "$c" ]; then printf '%s\n' "$c"; return 0; fi
    done
    return 1
}

CMAKE_BIN=$(find_cmake) || { echo "dep_fixture_test: cmake not found (set SICNU_CMAKE)" >&2; exit 1; }
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
failed=0

run_case() { # run_case <name> <source-dir> <build-dir> <extra args...>
    local name=$1 sdir=$2 bdir=$3; shift 3
    "$CMAKE_BIN" -S "$sdir" -B "$bdir" "$@" > "$bdir.log" 2>&1
}

has() { grep -qE "$1" "$2"; }

echo "dep_fixture_test: cmake=$CMAKE_BIN"

# ---- case 1: missing dependency -> actionable failure ----------------------
run_case missing "$fixture_dir" "$work_dir/missing" -DSICNU_FIXTURE_CASE=missing -DCMAKE_DISABLE_FIND_PACKAGE_GDAL=ON && rc=0 || rc=$?
if [ "$rc" != 0 ] \
   && has "Could NOT find GDAL \(required by this project\)" "$work_dir/missing.log" \
   && has "apt install libgdal-dev|brew install gdal|pacman -S gdal|vcpkg install gdal" "$work_dir/missing.log"; then
    echo "  ok: missing dependency fails with per-platform install guidance"
else
    echo "  FAIL: missing dependency did not produce actionable guidance (rc=$rc)"; failed=1
    tail -15 "$work_dir/missing.log"
fi

# ---- case 2: stale version -> minimum-version diagnosis --------------------
run_case stale "$fixture_dir" "$work_dir/stale" -DSICNU_FIXTURE_CASE=stale -DFakeDep_DIR="$fixture_dir/cmake" && rc=0 || rc=$?
if [ "$rc" != 0 ] \
   && has "Could NOT find FakeDep" "$work_dir/stale.log" \
   && has "Minimum version: 2\.0" "$work_dir/stale.log"; then
    echo "  ok: stale version fails naming the minimum"
else
    echo "  FAIL: stale version diagnosis missing (rc=$rc)"; failed=1
    tail -15 "$work_dir/stale.log"
fi

# ---- case 3: satisfied minimum -> success + summary (negative control) -----
run_case ok "$fixture_dir" "$work_dir/ok" -DSICNU_FIXTURE_CASE=ok -DFakeDep_DIR="$fixture_dir/cmake" && rc=0 || rc=$?
if [ "$rc" = 0 ] \
   && has "=== SICNU dependency summary ===" "$work_dir/ok.log" \
   && has "FakeDep: 1\.0" "$work_dir/ok.log"; then
    echo "  ok: satisfied dependency configures and prints the summary"
else
    echo "  FAIL: negative control case failed (rc=$rc)"; failed=1
    tail -15 "$work_dir/ok.log"
fi

# ---- case 4: the wrapped call behaves exactly like the direct call --------
# cmake/FindSqlite3.cmake enforces "run once" through a directory-scope
# variable, and vcpkg's find_package override (a MACRO) re-enters discovery for
# transitive dependencies. A function-scoped wrapper therefore breaks the
# direct-call equivalence; this case pins the macro-based scope discipline
# (regression: configure died with "SQLite::SQLite3 target should not have been
# defined at this point" under the vcpkg toolchain).
probe_dir="$repo_root/tests/fixtures/sqlite3_probe"
for mode in direct wrapped; do
    run_case "scope-$mode" "$probe_dir" "$work_dir/scope-$mode" -DPROBE_MODE="$mode" && rc=0 || rc=$?
    if [ "$rc" = 0 ] && has "PROBE $mode OK" "$work_dir/scope-$mode.log"; then
        echo "  ok: sqlite3 probe ($mode) configures"
    else
        echo "  FAIL: sqlite3 probe ($mode) failed (rc=$rc)"; failed=1
        tail -15 "$work_dir/scope-$mode.log"
    fi
done

if [ "$failed" = 0 ]; then
    echo "dep_fixture_test: ALL PASS"
    exit 0
fi
echo "dep_fixture_test: FAILED" >&2
exit 1
