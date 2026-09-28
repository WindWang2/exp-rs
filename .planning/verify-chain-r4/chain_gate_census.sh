#!/usr/bin/env bash
# chain_gate_census.sh — Track 16 gate-integrity census (v3, R3).
#
# Defect class pinned here (R2 finding): a Catch2 suite registered without a
# TEST_PREFIX produces bare English ctest case names, so name-based track
# gates (`ctest -R "verif|grader|preflight|suitab|science_context|evidence"`,
# D6) silently miss whole targets — on the R2 base that was ~270 cases across
# 29 targets, including the six-module-core test_science_context_broker.
# Some suites escaped only PARTIALLY, because a case name happened to contain
# a gate keyword ("preflightAlgorithm ...", "evidence projector ...").
#
# v3 changes (R3, drift-driven): v2 derived the FORWARD target list by
# grepping tests/CMakeLists.txt for single-line `sicnu_add_*`/`add_executable`
# registrations. Targets registered through a foreach loop
# (`add_executable(${_var} ...)` — the r4:: operator-oracle block) are
# invisible to that grep, so a future foreach suite with a D6-matching binary
# name and non-keyword case names would blind BOTH v2 rules. v3 therefore:
#   1. FORWARD is population-derived: every built discovery file whose target
#      name matches D6 must register ALL its cases under "<target>::".
#      This covers every registration channel that produces a discovery file.
#   2. BUILD-GAP keeps v2's signal: every D6-matching target found in the
#      CMakeLists enumeration must have a discovery file in the build dir
#      (a missing file is the _NOT_BUILT signal, D-R2-5).
#   3. REVERSE and the r4:: gate are unchanged from v2.
#
# Usage: chain_gate_census.sh <build-dir>   (default: build)
set -u

BUILD_DIR="${1:-build}"
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DOMAIN_RE='verif|grader|preflight|suitab|science|evidence'

# Allowlist: target -> reason (verified under a different, explicit gate, or
# not a Catch2 suite at all).
allow() {
    case "$1" in
        test_operator_preflight_refusals)
            # Track 7 (R4 operator oracles): D15 convention selects these via
            # `ctest -R "^r4::"`; the D6 regex matching its binary name is
            # incidental, not intended.
            return 0 ;;
        test_teaching_fake_grader_cli)
            # CLI helper binary (fixture generator), not a Catch2 suite; it
            # registers no cases of its own.
            return 0 ;;
        *) return 1 ;;
    esac
}

cd "$REPO_ROOT" || exit 1

overall=0

# --- 1. FORWARD (population-derived, v3): built discovery files ------------
printf '%-46s %8s %10s  %s\n' TARGET CASES PREFIXED VERDICT
found=0
while read -r f; do
    [ -f "$f" ] || continue
    t=$(basename "$f" | sed -E 's/-[a-f0-9]+_tests\.cmake$//')
    case "$t" in
        *verif*|*grader*|*preflight*|*suitab*|*science*|*evidence*) ;;
        *) continue ;;
    esac
    found=$((found + 1))
    total=$(grep -c '^add_test(' "$f")
    prefixed=$(grep '^add_test(' "$f" | grep -cF "$t::")
    if [ "$total" -gt 0 ] && [ "$total" -eq "$prefixed" ]; then
        printf '%-46s %8d %10d  OK\n' "$t" "$total" "$prefixed"
    elif allow "$t"; then
        printf '%-46s %8d %10d  ALLOWED (not %s-prefixed; owned by another gate)\n' "$t" "$total" "$prefixed" "$t"
    else
        printf '%-46s %8d %10d  FAIL (bare English names escape the D6 gate)\n' "$t" "$total" "$prefixed"
        overall=1
    fi
done < <(find "$BUILD_DIR" -maxdepth 2 -name '*_tests.cmake' 2>/dev/null | sort)
if [ "$found" -eq 0 ]; then
    echo "FORWARD: no discovery files found in $BUILD_DIR (wrong build dir?)"
    overall=1
fi

# --- 2. BUILD-GAP: CMakeLists-enumerated D6 targets must be built ----------
# Mechanical target enumeration from the test registration file: every name
# registered through ANY sicnu_add_* helper macro or plain/qt add_executable
# whose name matches the D6 domain regex (macro wildcard — review F2).
targets=$( {
    grep -hoE 'sicnu_add_[a-z_]+\([A-Za-z0-9_]+' tests/CMakeLists.txt |
        sed 's/.*(\(.*\)/\1/'
    grep -hoE 'add_executable\((test_[A-Za-z0-9_]+)' tests/CMakeLists.txt |
        sed 's/add_executable(\(.*\)/\1/'
    grep -hoE 'qt_add_executable\((test_[A-Za-z0-9_]+)' tests/CMakeLists.txt |
        sed 's/qt_add_executable(\(.*\)/\1/'
} | sort -u | grep -E "$DOMAIN_RE" || true)
gap=0
for t in $targets; do
    disc=""
    for f in "$BUILD_DIR"/tests/"$t"-*_tests.cmake "$BUILD_DIR"/"$t"-*_tests.cmake; do
        if [ -f "$f" ]; then disc="$f"; break; fi
    done
    if [ -z "$disc" ] && ! allow "$t"; then
        echo "BUILD-GAP: $t matches D6 but has no discovered cases in $BUILD_DIR (_NOT_BUILT)"
        gap=$((gap + 1))
        overall=1
    fi
done
[ "$gap" -eq 0 ] && echo "BUILD-GAP: 0 missing D6 targets"

# --- 3. REVERSE (v2, review F1): no bare keyword collision anywhere --------
collisions=0
while read -r f; do
    [ -f "$f" ] || continue
    t=$(basename "$f" | sed -E 's/-[a-f0-9]+_tests\.cmake$//')
    while read -r c; do
        [ -n "$c" ] || continue
        if ! allow "$t"; then
            echo "COLLISION $t :: $c (matches D6 by keyword, no :: prefix)"
            collisions=$((collisions + 1))
        fi
    done < <(grep '^add_test(' "$f" | sed 's/^add_test( \[==\[//; s/\]==\].*//' \
             | grep -E "$DOMAIN_RE" | grep -v '::')
done < <(find "$BUILD_DIR" -maxdepth 2 -name '*_tests.cmake' 2>/dev/null)
if [ "$collisions" -gt 0 ]; then
    echo "REVERSE RULE: $collisions keyword-collision case(s) in the built population"
    overall=1
else
    echo "REVERSE RULE: 0 bare keyword-collision cases in the built population"
fi

# --- 4. The D15 operator-oracle gate must stay selectable (review F6) ------
r4=$(ctest --test-dir "$BUILD_DIR" -N -R "^r4::" 2>/dev/null | grep -c 'Test *#')
echo "r4:: gate (D15 operator oracles): $r4 cases selectable (expect >= 1)"
if [ "$r4" -lt 1 ]; then
    echo "r4:: GATE LOST — operator oracle cases no longer selectable"
    overall=1
fi

if [ "$overall" -eq 0 ]; then
    echo "GATE CENSUS: ALL GREEN"
else
    echo "GATE CENSUS: FAILURES PRESENT"
fi
exit "$overall"
