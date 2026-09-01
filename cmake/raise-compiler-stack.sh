#!/bin/sh
# GCC 16.x segfaults ("internal compiler error: 段错误", no bug report
# dump) on deep Qt/QGIS template-heavy TUs when the build shell keeps the
# 8 MiB default stack: front-end/optimization recursion overflows it, and
# WHICH TUs die varies with the optimization level and include depth. Raise
# the soft stack limit where the OS allows and exec the real compiler —
# the limits are per-process, so this affects nothing but the compile.
ulimit -s 262144 2>/dev/null || ulimit -s unlimited 2>/dev/null || true
exec "$@"
