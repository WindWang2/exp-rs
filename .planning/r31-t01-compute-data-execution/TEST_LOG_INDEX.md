# Test Log Index

Base/HEAD for all entries below: `85ce917bea0ece4df6548a677b97a72ecde8659b` plus track-local planning/test additions (no production change yet).

| UTC | Command / target | Exit and case counts | Evidence |
|---|---|---|---|
| 2026-11-15 | Double guard (`host-build` + `build-dev`, cap 4) → `cmake --preset dev-default` | **1**, configure incomplete: system CMake could not discover OpenCV; no build/test ran. Not a source failure. | `.scratch/logs/configure_base.log` |
| 2026-11-15 | Same preset with confirmed `OpenCV_DIR=/home/kevin/projects/rs-studio/main/.scratch/ui-build-deps/opencv/usr/lib/cmake/opencv5`, wait 45 | **75**, host lock held by parallel T02; CMake did not start. Lock was not deleted or interrupted. | `.scratch/logs/configure_base_with_opencv.log` |
| 2026-11-15 | Same configure, guard wait 300 / host task timeout 300 | **TIMEOUT**, still waiting for T02's build lock; CMake did not start and the guard did not print a final refusal. | `.scratch/logs/configure_base_with_opencv_wait300.log` |
| 2026-11-15 | Same configure, guard wait 1800 | **75**, still waiting for the live T02 build; CMake did not start. A bounded wait-3600 retry `bash-57n2o6fa` is queued in the background. | `.scratch/logs/configure_base_with_opencv_wait1800.log`, successor `.scratch/logs/configure_base_with_opencv_wait3600.log` |

Future entries must include UTC time, base/full HEAD, exact command, double-lock configuration, exit code, discovered/executed/passed/failed/skipped case counts, and a local log path under `.scratch/` or a committed behavioral evidence document. Conditional targets that do not exist, 0-match CTest filters, NOT_BUILT and discovery failures are not passes.
