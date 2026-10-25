# LOOP_LEDGER — r22-teaching

Format: 轮次 | 假设 | 文件 | 测试命令 | 真实结果 | 下一步

- R0 | startup inventory complete; 5 issues triaged (4 OPEN_UNFIXED, #1558 FIXED_OPEN by 0847c5ec1) | .planning/r22-teaching/*.md | gh pr/issue queries + source reads | master=911e0b17a unchanged; 5 R21 PRs open, none touch our edit set | write RED tests
- R1 | #1694/#1689/#1688/#1678 each admits a RUNTIME red test against the current API (no compile-only red) | tests/test_r22_teaching_batch_regrade.cpp, tests/test_r22_teaching_guard_hygiene.cpp, tests/CMakeLists.txt (append) | (pending build) cmake configure ongoing | — | build test targets, run RED, commit
- R2 | configure needs vcpkg toolchain + VCPKG_INSTALLED_DIR=main build-dev tree (machine-proven recipe from .ps1_configure.ps1); first two attempts failed GDAL discovery (no toolchain) | build-r22-teaching | configure | FAILED PATH: vcpkg decided the shared tree's ABI hashes were stale and started a FROM-SOURCE rebuild storm (17 cl.exe on opencv4 → would take 1h+ AND overwrite the shared build-dev dependency tree, risking the main worktree's build). KILLED the run after ~20 min (4 small ports already reinstalled: same versions, ABI-compatible). | switch to classic mode
- R3 | classic-mode configure without vcpkg toolchain: consume build-dev/vcpkg_installed/x64-windows via CMAKE_PREFIX_PATH + explicit GDAL_DIR | build-r22-teaching | `cmake -S . -B build-r22-teaching -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DCMAKE_PREFIX_PATH=...Qt...;...;build-dev/vcpkg_installed/x64-windows -DGDAL_DIR=.../share/gdal` | exit=0, configure 105s + generate 56s | build test targets, expect #1696 baseline-red on sicnu_lab_runtime

