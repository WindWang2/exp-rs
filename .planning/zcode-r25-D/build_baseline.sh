#!/bin/bash
# R25-D baseline build: shared-host lock + -j2 cap + ccache via /usr/sbin/cc symlink.
# github.com is unreachable from this host (TLS eof), so the FetchContent deps
# reuse the sibling track's exact-version source cache (catch2 v3.7.1,
# pybind11 v2.13.6) through FETCHCONTENT_SOURCE_DIR_* — dependency sources
# shared, build directory strictly this worktree's own.
set -uo pipefail
cd /home/kevin/projects/rs-studio/exp-rs-r25-D-20261028T170355Z
export CMAKE_BUILD_PARALLEL_LEVEL=2
export QT_QPA_PLATFORM=offscreen
DEPS=/home/kevin/projects/rs-studio/exp-rs-r25-C-20261028T140339Z/build-dev/_deps
flock "$HOME/.cache/exp-rs-build.lock" bash -c "
  cmake -S . -B build-ci-fast -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON \
    -DFETCHCONTENT_SOURCE_DIR_CATCH2=$DEPS/catch2-src \
    -DFETCHCONTENT_SOURCE_DIR_PYBIND11=$DEPS/pybind11-src 2>&1 | tail -5
  echo \"=== CONFIGURE EXIT: \${PIPESTATUS[0]} ===\"
  cmake --build build-ci-fast --parallel 2 2>&1 | tail -30
  echo \"=== BUILD EXIT: \${PIPESTATUS[0]} ===\"
"
