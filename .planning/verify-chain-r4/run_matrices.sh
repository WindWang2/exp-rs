#!/usr/bin/env bash
# run_matrices.sh — Track 16 independent matrix re-runner.
#
# Re-runs the two chain matrices from a build directory and prints a
# per-matrix PASS/FAIL summary line each. Exit 0 only when both matrices are
# green in both rounds.
#
# Usage: run_matrices.sh <build-dir> [rounds]
#   <build-dir>  a configured build dir with ENABLE_TESTS=ON (default: build)
#   [rounds]     default 2 (key verifications run twice by track discipline)
set -u

BUILD_DIR="${1:-build}"
ROUNDS="${2:-2}"
LOCPATH_DIR="${LOCPATH_DIR:-/tmp/sicnu-track16-locpath}"

cd "$(dirname "$0")/../.." || exit 1 # repo root (script lives in .planning/verify-chain-r4/)

# Self-provision the comma-decimal locale (the anti-vacuity requirement for
# the locale matrix: the hostile locale must actually bite the formatter).
if [ ! -d "$LOCPATH_DIR/de_DE.UTF-8" ]; then
    mkdir -p "$LOCPATH_DIR"
    localedef -i de_DE -f UTF-8 "$LOCPATH_DIR/de_DE.UTF-8" 2>/dev/null \
        || echo "WARN: localedef failed; de_DE rows will degrade (the test WARNs loudly too)"
fi
export LOCPATH="$LOCPATH_DIR"

overall=0
for round in $(seq 1 "$ROUNDS"); do
    echo "=== round $round/$ROUNDS ==="
    for target in test_verify_chain_locale_matrix test_preflight_authority_invalidation; do
        bin="$BUILD_DIR/tests/$target"
        [ -x "$bin" ] || bin="$BUILD_DIR/$target"
        if [ ! -x "$bin" ]; then
            echo "MATRIX $target: FAIL (binary not built in $BUILD_DIR)"
            overall=1
            continue
        fi
        if "$bin" --reporter compact > /tmp/track16_$target.log 2>&1; then
            summary=$(grep -oE '[0-9]+ assertions in [0-9]+ test cases' /tmp/track16_$target.log | tail -1)
            echo "MATRIX $target round$round: PASS ($summary)"
        else
            echo "MATRIX $target round$round: FAIL"
            grep -E 'failed|FAILED' /tmp/track16_$target.log | head -5
            overall=1
        fi
    done
done

if [ "$overall" -eq 0 ]; then
    echo "ALL MATRICES GREEN ($ROUNDS rounds)"
else
    echo "MATRICES RED"
fi
exit "$overall"
