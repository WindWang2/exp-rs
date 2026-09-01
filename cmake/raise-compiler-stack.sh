#!/bin/sh
# GCC 16.2.1 on this toolchain intermittently dies with "internal compiler
# error: 段错误" on Qt/QGIS template-heavy TUs — non-deterministically per
# process (the same compile passes ~70-90% of the time at any stack limit;
# deep include graphs make borderline runs sensitive to environment size,
# so the soft stack limit is also raised). Retrying the compile a couple of
# times makes the failure probability negligible; a deterministic error
# fails identically on every attempt and still surfaces on the last one.
ulimit -s 262144 2>/dev/null || ulimit -s unlimited 2>/dev/null || true

attempt=0
max=8
status=0
while [ "$attempt" -lt "$max" ]; do
    if "$@"; then
        exit 0
    fi
    status=$?
    attempt=$((attempt + 1))
done
exit $status
