#!/usr/bin/env bash
# #660: one command that runs the full benchmark suite and writes a
# machine-readable baseline artifact (per-workload wall time + peak RSS +
# hardware metadata). Runs unchanged on the acceptance workstation and on a
# 1-core dev box (functional pass; numbers indicative only).
#
# Usage:
#   scripts/run_perf_baseline.sh [output.json]     # default: benchmarks/baseline-<host>.json
#   SICNU_BENCH_LARGE=1 scripts/run_perf_baseline.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOSTN="$(uname -n 2>/dev/null | cut -d. -f1)"
[[ -z "${HOSTN}" ]] && HOSTN=host
OUT="${1:-${REPO_ROOT}/benchmarks/baseline-${HOSTN}-$(date +%Y%m%d).json}"

# Locate a built test_perf_benchmarks (same order as pi/ binary detection).
BIN=""
for cand in \
    "${REPO_ROOT}/build/tests/test_perf_benchmarks" \
    "${REPO_ROOT}/build-wt/tests/test_perf_benchmarks" \
    "${REPO_ROOT}/build-dev/tests/test_perf_benchmarks" \
    "${REPO_ROOT}/build-ci-fast/tests/test_perf_benchmarks"; do
    if [[ -x "${cand}" ]]; then BIN="${cand}"; break; fi
done
if [[ -z "${BIN}" ]]; then
    echo "error: no built test_perf_benchmarks found (build the project first)" >&2
    exit 1
fi

mkdir -p "$(dirname "${OUT}")"
RAW="$("${BIN}" 2>/dev/null | grep '^\[bench\]' || true)"
if [[ -z "${RAW}" ]]; then
    echo "error: benchmark binary produced no [bench] lines" >&2
    exit 1
fi

CORES="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 0)"
RAM_KB="$(grep MemTotal /proc/meminfo 2>/dev/null | awk '{print $2}' || echo 0)"
RAM_MB=$(( RAM_KB / 1024 ))

# [bench] <name>  time=  1.234s  peakRSS=  123MB  (delta=  100MB)
{
    echo "{"
    echo "  \"generatedUtc\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
    echo "  \"gitSha\": \"$(git -C "${REPO_ROOT}" rev-parse HEAD 2>/dev/null || echo unknown)\","
    echo "  \"hardware\": { \"cores\": ${CORES}, \"ramMb\": ${RAM_MB}, \"host\": \"$(hostname -s)\" },"
    echo "  \"largeMode\": \"${SICNU_BENCH_LARGE:-0}\","
    echo "  \"workloads\": ["
    first=1
    while IFS= read -r line; do
        name="$(echo "${line}" | awk '{print $2}')"
        t="$(echo "${line}" | sed -n 's/.*time=[[:space:]]*\([0-9.]*\)s.*/\1/p')"
        peak="$(echo "${line}" | sed -n 's/.*peakRSS=[[:space:]]*\([0-9]*\)MB.*/\1/p')"
        delta="$(echo "${line}" | sed -n 's/.*delta=[[:space:]]*\([0-9]*\)MB.*/\1/p')"
        [[ -z "${name}" || -z "${t}" ]] && continue
        [[ ${first} -eq 0 ]] && echo ","
        first=0
        printf '    { "name": "%s", "seconds": %s, "peakRssMb": %s, "deltaRssMb": %s }' \
            "${name}" "${t}" "${peak:-0}" "${delta:-0}"
    done <<< "${RAW}"
    echo ""
    echo "  ]"
    echo "}"
} > "${OUT}"

echo "baseline written: ${OUT}"
grep -c '"name"' "${OUT}" | xargs -I{} echo "workloads captured: {}"
