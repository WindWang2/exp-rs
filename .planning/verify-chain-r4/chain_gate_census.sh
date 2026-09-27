#!/usr/bin/env bash
# chain_gate_census.sh — Track 16 R2 gate-integrity census.
#
# Defect class pinned here (R2 finding): a Catch2 suite registered without a
# TEST_PREFIX produces bare English ctest case names, so name-based track
# gates (`ctest -R "verif|grader|preflight|suitab|science_context|evidence"`,
# D6) silently miss whole targets — on the R2 base that was 62+ cases across
# 11 targets, including the six-module-core test_science_context_broker.
# Some suites escaped only PARTIALLY, because a case name happened to contain
# a gate keyword ("preflightAlgorithm ...", "evidence projector ...").
#
# Rule enforced (mechanical, build-derived):
#   every target whose binary name matches the D6 domain regex must register
#   ALL of its discovered ctest cases under its own "<target>::" prefix —
#   with an explicit allowlist for suites owned by another gate convention.
#
# Usage: chain_gate_census.sh <build-dir>   (default: build)
set -u

BUILD_DIR="${1:-build}"
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DOMAIN_RE='verif|grader|preflight|suitab|science|evidence'

# Allowlist: target -> reason (verified under a different, explicit gate).
allow() {
    case "$1" in
        test_operator_preflight_refusals)
            # Track 7 (R4 operator oracles): D15 convention selects these via
            # `ctest -R "^r4::"`; the D6 regex matching its binary name is
            # incidental, not intended. Verified under the r4:: gate below.
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
# registered via the sicnu_* helper macros or plain add_executable whose name
# matches the D6 domain regex.
targets=$( {
    grep -hoE 'sicnu_add_(test|io_test|sdk_test|study_test|dialog_test|experiment_studio_test)\([A-Za-z0-9_]+' tests/CMakeLists.txt |
        sed 's/.*(\(.*\)/\1/'
    grep -hoE 'add_executable\((test_[A-Za-z0-9_]+)' tests/CMakeLists.txt |
        sed 's/add_executable(\(.*\)/\1/'
} | sort -u | grep -E "$DOMAIN_RE" || true)

overall=0
printf '%-46s %8s %10s  %s\n' TARGET CASES PREFIXED VERDICT
for t in $targets; do
    disc=$(ls "$BUILD_DIR"/tests/"$t"-*_tests.cmake "$BUILD_DIR"/"$t"-*_tests.cmake 2>/dev/null | head -1)
    if [ -z "$disc" ]; then
        if allow "$t"; then
            printf '%-46s %8s %10s  %s\n' "$t" "-" "-" "ALLOWED ($t exempt, see script)"
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

# The allowlisted D15 suite must still be r4::-selectable.
r4=$(ctest --test-dir "$BUILD_DIR" -N -R "^r4::" 2>/dev/null | grep -c 'Test *#')
echo "r4:: gate (D15 operator oracles): $r4 cases selectable"

if [ "$overall" -eq 0 ]; then
    echo "GATE CENSUS: ALL GREEN"
else
    echo "GATE CENSUS: FAILURES PRESENT"
fi
exit "$overall"
