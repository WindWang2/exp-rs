#!/bin/bash
# R25-D targeted build: reconfigure (pick up new test registrations) + build
# ONLY this track's test targets and the neighborhood regression targets.
# Shared-host lock + -j2 cap; ccache via /usr/sbin/cc symlink.
set -uo pipefail
cd /home/kevin/projects/rs-studio/exp-rs-r25-D-20261028T170355Z
export CMAKE_BUILD_PARALLEL_LEVEL=2
export QT_QPA_PLATFORM=offscreen
DEPS=/home/kevin/projects/rs-studio/exp-rs-r25-C-20261028T140339Z/build-dev/_deps
TARGETS="test_r25_d_cartography_ux test_r25_d_cartography_dock test_r25_d_operator_preflight test_r25_d_result_open_link test_r25_d_canvas_invalidation test_r25_d_e2e_display_cartography"
NEIGHBORHOOD="test_mapspec test_qgis_display_manager test_command_registry test_selection_context test_command_palette test_shortcut_conflicts test_workbench_full_shell_lifecycle test_cartography_operators_10"
flock "$HOME/.cache/exp-rs-build.lock" bash -c "
  cmake -S . -B build-ci-fast -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON \
    -DFETCHCONTENT_SOURCE_DIR_CATCH2=$DEPS/catch2-src \
    -DFETCHCONTENT_SOURCE_DIR_PYBIND11=$DEPS/pybind11-src 2>&1 | tail -3
  echo \"=== CONFIGURE EXIT: \${PIPESTATUS[0]} ===\"
  cmake --build build-ci-fast --parallel 2 --target $TARGETS $NEIGHBORHOOD 2>&1 | tail -25
  echo \"=== BUILD EXIT: \${PIPESTATUS[0]} ===\"
"
