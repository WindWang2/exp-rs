# CROSS_TRACK_FINDINGS — R23-E（跨轨反馈，只记录不改他轨文件）

## 1. #1699 残余墙钟断言（9 处，全部 C/D 轨测试文件 — E12 规则：跨轨测试只读）

沿用 #1708 定型的三分法转换（`tests/sicnu_test_time_budget.h` 预算 / hang-guard / 结构分离）：

| file:line | 断言 | 归属轨 | 建议转换 |
|---|---|---|---|
| tests/test_model_failure_matrix.cpp:1000 | `CHECK(elapsed < 500)` | C/D（模型失败矩阵） | time_budget（sanitizer ×4） |
| tests/test_execution_governor_11.cpp:376 | `REQUIRE(elapsedMs < 500)` | C（governor） | hang-guard 500ms/10s（紧随结构断言） |
| tests/test_temporal_profile_widget.cpp:170 | `REQUIRE(elapsedMs < 200)` | C（GUI widget） | 记录型或 time_budget |
| tests/test_feature_scatter.cpp:111 | `REQUIRE(elapsedMs < 40)` | C（GUI） | 40ms 过紧，sanitizer 必炸；time_budget 或删（配结构断言） |
| tests/test_progress_dialog.cpp:112 | `CHECK(dialog.elapsedMs() < 100)` | C（GUI） | 同上 |
| tests/test_adversarial_m5.cpp:173/181 | `elapsed < 200` 条件分支 | C（adversarial） | 结构化等待 helper（#1656 模式） |
| tests/test_tool_call_dispatcher.cpp:990 | `REQUIRE(elapsed.count() < 2000)` | C（dispatcher） | hang-guard |
| tests/test_agent_ops_ui_smoke.cpp:72 | `CHECK(elapsed < 200)` | C（UI smoke） | time_budget |

## 2. #1682 残余（Windows 编译型测试子集需依赖栈重构）

#1708 CROSS_TRACK #5 已记录。本轨补证：Windows 真机 .cmd 守卫腿已在真 cmd.exe 执行（TEST_RESULTS T3）。Windows 编译型测试子集仍需 vcpkg 依赖工作流抽取（C/E 共同面）。

## 3. #1623 残留验证路径

.qm install 规则在根 CMakeLists（978-1011）。E 轨将在 WP4 实机 install 树中断言 `sicnu_zh_CN.qm` 存在（回归 guard 为事实证据而非新规则）。若 WP4 发现缺失 → 本轨最小修复（根 CMakeLists 属共享面，最小改动+三方核验）。

## 4. #1683 残余：根目录遗留工程脚本（E 域盘点，不动）

14 个 tracked 根目录 .cmd（build.cmd、build_r6.cmd、build_r8sci.cmd、build_r8sci2.cmd、build_wb7.cmd、configure_build.cmd、configure_r6.cmd、configure_r8sci.cmd、configure_wb7.cmd、r6_dev.cmd、run_exe.cmd、run_tests.cmd、run_wb7_tests.cmd、test_wb7.cmd）+ 历史 build_*.log/.obj/.pdb 疑似未 tracked（repo 根另见 configure.log/regression.log/tmp_*.log/vc140.pdb 等）。这些是历次 epic 的机器特定快捷方式（引用 C:\deps\Qt\6.8.0 等本机路径），与 CMakePresets/CLAUDE.md 快捷命令重复。建议：跨轨确认无人引用后统一 `git rm`（E 轨原则"禁止删除未标记文件"+ 并行 epic 风险 → 本轨只盘点不删）。

## 5. tests/CMakeLists.txt 注册面事实（供各轨 push 前自查）

当前 master（ad98a7a70）文字态注册目标 1038 个（scripts/check_merge_registration_integrity.py 计数）。本轨新增 3 个注册（test_r23_e_plugin_typed_gates + 2 个 add_test 脚本门）。建议各轨 push 前运行该脚本（--base origin/master）。

## 6. WSL 构建宿主 python3.14 缺 numpy/pip（环境观察）

daemon（src/python/scripts/worker_daemon.py）顶层 `import numpy`，但宿主系统 python 无 pip 无 numpy（ensurepip 亦损坏）。任何驱动真 daemon 的 ctest（本轨 r23_e_python_worker_typed、r22 的 ghost/ipc 族）在此宿主需 numpy 装配。本轨以用户目录 wheel 解决（不入库）；CI/其他宿主由 install_deps 侧保障。建议：infra 面考虑 install_deps.sh 或 CI 里钉 python-numpy（C 轨 CI 域）。
