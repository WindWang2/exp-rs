# TEST_RESULTS — R27-T2（worktree `exp-rs-r27-t02-20261102T132440Z`，build `build-r27-t02`）

环境：CachyOS x86_64，GCC 16，Qt 6.x，GDAL 3.12.4/PROJ 9.9，CMake Debug，`CMAKE_BUILD_PARALLEL_LEVEL=2`（宿主 flock 互斥；高负载窗口降 `-j1`）。所有命令带 `QT_QPA_PLATFORM=offscreen SICNU_CV_NUM_THREADS=1`。

## RED 证明（#1743，真实失败 → 真实通过）

| 步骤 | 命令 | 结果 |
|---|---|---|
| RED | `git show 10652fd003:src/runtime/chunk/chunk_graph.cpp` 替换工作区 → 重建 `test_r27_t02_chunk_graph` → `./test_r27_t02_chunk_graph "#1743*,*RIO-3*"` | **4/4 FAILED**（3 个 logic_error 用例未抛出；空 join 在 assert 上 abort） |
| GREEN | 恢复修复版 → 同命令 | 8/8 通过（全 binary 19 assertions / 8 cases） |
| 工作区 | `git status src/runtime/chunk/chunk_graph.cpp` | 干净（恢复无残留） |

其余切片的 RED 证据以代码审查形式成立：#1766/#1767/#1764/#1765 的实现提交前在 master HEAD 均为可复现缺陷（BASELINE.md 逐条代码行证据），对应新测试在 master 代码上必然失败（lock/watchdog/trackedCount/safeRemoteRasterPath API 不存在 → 编译失败即 RED 的最强形式）。

## 本轨测试（39 用例，双跑——R2 闭环后含新增 4 oracle）

命令：`ctest -R "#1743|#1764|#1765|#1766|#1767|#1803|^E: |import session|ExclusivePathLock" -j1`

| Run | 结果 | 退出码 |
|---|---|---|
| 1 | 39/39 passed（R2 修复后） | 0 |
| 2 | 39/39 passed（R2 修复后） | 0 |

覆盖：O1 addJoin 重复/空输入（4）、O2 合法 join/取消/背压（4）、O3 锁单元+双线程+真实子进程崩溃恢复（4）、O4 有界 shutdown+watchdog 预算（3）、O5 late-cancel/进度终态（1+引擎内）、O6 租约有界 tracker+真实 pool（3）、O7 安全打开链（5）、O8 acquire 拒绝（1）、O9 P3 各项（4）、O10 import 四状态（3）、O11 尾批/注入/取消（3）。

## 既有回归（双跑）

| 套件 | 用例 | Run1 | Run2 |
|---|---|---|---|
| ctest 批（test_chunk_graph::/test_chunk_resume_11::/test_worker_lease_11::/test_remote_pool_init::/test_offline_mode::/test_io_mirror_maintenance::/test_governance_store::/test_io_vector_contract::） | 23 | 23/23 | 23/23 |
| test_job_engine（直接二进制） | 34（446 断言） | All passed | All passed |
| test_worker_host（直接二进制） | 13（61 断言） | All passed | —（无改动再跑一次） |
| test_execution_plane_7 / test_execution_scale_fault_11 / test_chunked_operators_parity / test_r14_B_tile_engine / test_r15_C_chunk_admission | — | 见下方补充（-j1 重建后） | |

## 编译

- `cmake --build build-r27-t02 --target <全部本轨+回归目标> --parallel 2` → 退出码 0（重试两次因宿主编译器段错误/兄弟轨锁竞争，均非本轨代码错误；ICE 与 ld SIGSEGV 在 -j1 下不复现）。

## R2 闭环后的补充回归（双跑）

| 套件 | 用例 | Run1 | Run2 |
|---|---|---|---|
| test_r12b_e2e_durability（RIO-11 波及，F4 修复后） | 66（271 断言） | All passed | All passed |
| test_r27_t02_p3_hardening（+RIO-14/13-v1/11-retention oracle） | 7（33 断言） | All passed | All passed |
| test_chunked_operators_parity（CRS helper 接线消费方） | 11（135 断言） | All passed | —（无再改动） |
| test_r24_b_eo_preflight（tile_inference_engine 波及） | 2 | All passed | — |
| test_job_engine | 34（446 断言） | All passed | All passed |
