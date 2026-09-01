#!/bin/sh
# GCC 16.2.1 on this toolchain intermittently dies with "internal compiler
# error: segfault" on Qt/QGIS template-heavy TUs. Two contributing factors:
# front-end recursion depth (raise the soft stack limit) and the initial
# stack offset, which shifts with the inherited environment size — builds
# launched from wrapper environments (IDEs, AppImages) carry kilobytes of
# extra variables that push borderline compiles over. The launcher therefore
# (a) raises the limit, (b) execs the compiler with a trimmed environment,
# and (c) retries — the crash is per-process random, so a few rolls make
# the residual probability negligible. Deterministic compile errors fail
# identically on every attempt and still surface on the last one.
ulimit -s 262144 2>/dev/null || ulimit -s unlimited 2>/dev/null || true

attempt=0
max=12
status=0
while [ "$attempt" -lt "$max" ]; do
    if env -i PATH="$PATH" HOME="${HOME:-/root}" TERM="${TERM:-dumb}" \
         LANG="${LANG:-C.UTF-8}" LC_ALL="${LC_ALL:-C.UTF-8}" TMPDIR="${TMPDIR:-/tmp}" "$@"; then
        exit 0
    fi
    status=$?
    attempt=$((attempt + 1))
done
exit $status
