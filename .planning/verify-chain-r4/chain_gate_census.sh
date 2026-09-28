#!/usr/bin/env bash
# chain_gate_census.sh — Track 16 R2 gate-integrity census (v2, review-hardened).
#
# Defect class pinned here (R2 finding): a Catch2 suite registered without a
# TEST_PREFIX produces bare English ctest case names, so name-based track
# gates (`ctest -R "verif|grader|preflight|suitab|science_context|evidence"`,
# D6) silently miss whole targets — on the R2 base that was ~270 cases across
# 29 targets, including the six-module-core test_science_context_broker.
# Some suites escaped only PARTIALLY, because a case name happened to contain
# a gate keyword ("preflightAlgorithm ...", "evidence projector ...").
#
# Two enforced rules (mechanical, build-derived):
#   1. FORWARD: every target whose binary name matches the D6 domain regex
#      must register ALL of its discovered ctest cases under its own
#      "<target>::" prefix — with an explicit allowlist for suites owned by
#      another gate convention (v2: registration macros enumerated by
#      wildcard so future sicnu_add_* helpers cannot bypass the census).
#   2. REVERSE (v2, review F1): anywhere in the built population, a case
#      whose name matches the D6 regex without a "::" prefix is a gate
#      collision — keyword-accident selection — and fails the census unless
#      allowlisted. Run against a full build, this flags cross-domain
#      keyword collisions (agent/cartography/... suites) too.
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

# Mechanical target enumeration from the test registration file: every name
# registered through ANY sicnu_add_* helper macro or plain add_executable
# whose name matches the D6 domain regex (v2: macro wildcard — review F2).
targets=$( {
    grep -hoE 'sicnu_add_[a-z_]+\([A-Za-z0-9_]+' tests/CMakeLists.txt |
        sed 's/.*(\(.*\)/\1/'
    grep -hoE 'add_executable\((test_[A-Za-z0-9_]+)' tests/CMakeLists.txt |
        sed 's/add_executable(\(.*\)/\1/'
} | sort -u | grep -E "$DOMAIN_RE" || true)

overall=0
printf '%-46s %8s %10s  %s\n' TARGET CASES PREFIXED VERDICT
for t in $targets; do
    # v2 (review F7): pick the first EXISTING discovery file; a glob can
    # match several build configurations — never feed a multi-line string
    # to grep as a filename.
    disc=""
    for f in "$BUILD_DIR"/tests/"$t"-*_tests.cmake "$BUILD_DIR"/"$t"-*_tests.cmake; do
        if [ -f "$f" ]; then disc="$f"; break; fi
    done
    if [ -z "$disc" ]; then
        if allow "$t"; then
            printf '%-46s %8s %10s  %s\n' "$t" "-" "-" "ALLOWED (exempt, see script)"
            continue
        fi
        printf '%-46s %8s %10s  %s\n' "$t" "-" "-" "FAIL (no discovered cases in $BUILD_DIR)"
        overall=1
        continue
    fi
    total=$(grep -c '^add_test(' "$disc")
    prefixed=$(grep '^add_test(' "$disc" | grep -cF "$t::")
    if [ "$total" -gt 0 ] && [ "$total" -eq "$prefixed" ]; then
        printf '%-46s %8d %10d  OK\n' "$t" "$total" "$prefixed"
    elif allow "$t"; then
        printf '%-46s %8d %10d  ALLOWED (not %s-prefixed; owned by another gate)\n' "$t" "$total" "$prefixed" "$t"
    else
        printf '%-46s %8d %10d  FAIL (bare English names escape the D6 gate)\n' "$t" "$total" "$prefixed"
        overall=1
    fi
done

# REVERSE rule (v2, review F1): no case anywhere in the built population may
# match the D6 regex without a "::" prefix (keyword-accident selection).
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

# The allowlisted D15 suite must stay selectable (v2, review F6: enforced).
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
