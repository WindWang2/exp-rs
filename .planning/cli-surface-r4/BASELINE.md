# BASELINE — Track 14: CLI Surface Completion R4

开工时间：2026-09-27（UTC+8 会话）。全部数字为本 worktree 实测，非沿用提示词写作值。

## 1. 实测基线

| 项 | 实测值 |
|---|---|
| `origin/master` | `15e5c66b543ef3874cb929f17529ef456bd6c059`（PR #1333 合并点，与写作值一致，未前进） |
| 本地 master | 落后 origin/master 159 提交（`git rev-list --count HEAD..origin/master`，本地 master 未用） |
| 本分支 | `hardening/r4-cli-surface`，基于 `origin/master`，独立 worktree `../exp-rs-cli-surface-r4` |
| 开放 issue | **0**（`gh issue list --state open` 为空，实测） |
| 开放 PR | **7 个**：#1334、#1335、#1336、#1337、#1338、#1339、#1340（比写作时多 4 个） |
| 构建目录 | `build-gcc15`（`-DCMAKE_CXX_COMPILER=g++-15`，Debug，Ninja，`ENABLE_TESTS=ON`，`CMAKE_CXX_COMPILER_LAUNCHER=` 显式置空绕开 raise-compiler-stack.sh 假绿陷阱；gcc-15 下该脚本本身也是 no-op，双保险） |

## 2. 在途 PR 重叠图（file-overlap map）

| PR | 分支 | 与本轨道白名单重叠的文件 | 本轨道策略 |
|---|---|---|---|
| #1334 fix/review-p1-security | `src/cli/rs_pipeline_runner.cpp`、`tests/test_pipeline_runner.cpp`、`tests/CMakeLists.txt`、`docs/agents/surface-contracts.md`、`docs/USER_GUIDE.md` | **不重复安全改动**：containment/沙箱语义只消费不修改；`pipeline` 命令 WP 只测退出码与错误表面；`tests/CMakeLists.txt` 改动（本轨道为新测试注册）rebase 时序写入 PR 正文 |
| #1335 fix/review-p0-build-restore | `tests/CMakeLists.txt` | 无源码重叠；注册 hunk rebase 可机械收敛 |
| #1336 hardening/closure-ui-runtime-r4 | `data/help/commands.json`（5 条 GUI shell 命令 help gap）、`tests/test_help_integrity_12.cpp` | 已关闭的 5 gap 是 **GUI shell 命令**（mission.task.resume / mission.task.retry / mission.timeline.show / teaching.labCockpit.show / workbench.experimentExplorationStudio），非 CLI 面——本轨道不重修 |
| #1337 hardening/closure-workflow-contracts-r4 | `data/help/commands.json`、`data/help/diagnostics.json`、`src/contracts/error_code_scanner.*` | error_code_scanner 属 contracts 轴不碰；commands.json 重叠仅 GUI 词表条目 |
| #1338 hardening/closure-io-processing-r4 | 无 src/cli 重叠（data/geospatial/io 面） | 不相交 |
| #1339 hardening/r4-i18n-help | `data/help/commands.json`（76→81，shell 注册表对齐）、`tests/test_help_commands_schema.cpp`、`tests/CMakeLists.txt` | **GUI/shell 词表（81/81）归 #1339**，本轨道不重做该轴；本轨道 WP-C 走 CLI 三级面（见 §5 重定版） |
| #1340 hardening/r4-operator-oracles | `tests/CMakeLists.txt`、`.planning/**`（其自己的目录） | 无 src/cli 重叠 |

**结论**：`src/cli/` 29 文件中仅 `rs_pipeline_runner.cpp` 与 #1334 重叠（表面只读消费）；`data/help/commands.json` 三方重叠但 CLI 轴面（本轨道）与 GUI 词表轴面（#1339）正交；`tests/CMakeLists.txt` 多方重叠 → 本轨道新测试注册集中放在文件尾单一 hunk，PR 正文写明 rebase 时序。

## 3. src/cli 文件清单（实测 29 个，写作值 30）

实现 TU（13）：cli_agent_ops_commands、cli_batch_runner、cli_commands、cli_dataset_commands、cli_env_doctor、cli_lab_commands、cli_passport_commands、cli_project_ops、cli_tool_commands、help_cli_projections、lab_batch_runner、lab_report_runner、lab_self_check。
头文件（9 对应 + 独立）：上述 .h + rs_pipeline_runner.{h,cpp}、main_cli.cpp、sicnu_worker_main.cpp、CMakeLists.txt。

## 4. 19 顶层命令 ↔ 实现映射表（dispatch 链实测 `cli_commands.cpp:2640-2696`）

| # | 命令 | dispatch 行 | 实现 TU | 实现函数 |
|---|---|---|---|---|
| 1 | algorithms | 2648 | cli_commands.cpp | commandAlgorithms |
| 2 | run | 2650 | cli_commands.cpp | commandRun |
| 3 | pipeline | 2652 | cli_commands.cpp（走 rs_pipeline_runner） | commandPipeline |
| 4 | workflow | 2654 | cli_commands.cpp | commandWorkflow |
| 5 | plugin | 2656 | cli_commands.cpp | commandPlugin |
| 6 | models | 2658 | cli_commands.cpp | commandModels |
| 7 | project | 2660 | cli_commands.cpp（cli_project_ops 协作） | commandProject |
| 8 | data | 2662 | cli_commands.cpp | commandData |
| 9 | data-providers | 2664 | cli_commands.cpp | commandDataProviders |
| 10 | dataset | 2666 | cli_dataset_commands.cpp:690 | commandDataset |
| 11 | experiment | 2668 | cli_dataset_commands.cpp:700 | commandExperiment |
| 12 | reproduce | 2670 | cli_dataset_commands.cpp:706 | commandReproduce |
| 13 | lab | 2672 | cli_lab_commands.cpp（+lab_batch_runner/lab_report_runner/lab_self_check） | commandLab |
| 14 | env-doctor | 2674 | cli_env_doctor.cpp | commandEnvDoctor |
| 15 | tools | 2676 | cli_tool_commands.cpp | commandTools |
| 16 | passport | 2678 | cli_passport_commands.cpp | commandPassport |
| 17 | batch | 2680 | cli_batch_runner.cpp | commandBatch |
| 18 | session | 2682 | cli_agent_ops_commands.cpp | commandAgentSession |
| 19 | catalog | 2685 | cli_commands.cpp | commandCatalogExport |

## 5. 关键实测发现（改变 WP 落点的证据）

1. **退出码合同已存在**：`src/sdk/exprs/exit_codes.h`（0=Ok/1=GenericError/2=ValidationFailure/3=ExecutionFailure/4=Cancelled/5=MissingDependency/6=InvalidInput/7=RuntimeUnavailable），`docs/headless/README.md:60-71` 已发布同表。**提示词的 0/2/3/4/5 分类作废**，WP-A 改为对既有 8 值合同的逐命令符合度审计（见 DECISIONS D1）。
2. **`cli_commands.cpp` 纪律良好但子命令 TU 不齐**：cli_commands.cpp 用 `ExitCode::` 84 处、裸数字 return 0 处；`cli_dataset_commands.cpp` 0 处 `ExitCode::`，本地 `fail()`（:160）把全部失败硬编码为 **1**，`reproduce` 二值 `0/1`；`rs_pipeline_runner.cpp`、`lab_self_check.cpp` 也 0 处。这就是"退出码语义不统一"的精确落点。
3. **dispatch 层无外层异常边界**：`dispatchCliCommand`（:2640）与 `main_cli.cpp`（`int main` :70，全 TU 零 catch）都没有兜底 catch → 子命令 TU 未捕获异常直达 `std::terminate`（SIGABRT≈134）。**WP-F 的头号缺陷**。既有 catch 点共 18 处（cli_commands 11、cli_batch_runner 2、lab_batch_runner 2、sicnu_worker_main 1 等），逐处审计吞异常/裸字符串问题。
4. **错误出口形态**：`CliIO::finish`（:2594）JSON 信封含 `ok/command/error/data/diagnostics/api_version`；`error` 是自由字符串，非 JSON 模式裸打印到 stderr。**四元组（错误码+原因+期望/实际+建议）尚不存在**，WP-B 落点 = 在 finish 出口加结构化错误对象 + usage 段，逐路径迁移。
5. **词表真相**：`data/help/commands.json`（实测 76 条，id 形如 `command.project.new`）是 **CommandRegistry（GUI shell，#1339 实测 81 id）** 的帮助词表，由 `src/help/command_help_provider.cpp`（加法知识叠加）与 `tests/test_command_contract_9.cpp:162`（`command.<id>`→`<id>` 对账）、`test_help_coverage`（双向 diff）、`test_help_commands_schema`（#1339 新增）守护。**CLI 命令不在此词表内**；CLI 词表面 = `docs/headless/README.md`（C-3 已有 test_cli_command_surface 对账）+ 进程 help 输出 + 逐命令 usage 串。WP-C 重定版见 DECISIONS D2。
6. **既有测试载体**：`test_cli_agent_ops`、`test_cli_batch_manifest`、`test_cli_commands_json`、`test_cli_command_surface`、`test_generic_cli_manifest`、`test_lab_report_cli`、`test_mlops9_cli_record`、`test_global_help_tips`、`test_help_core`、`test_help_coverage`、`test_help_integrity_12`、`test_help_system`、`test_llm_streaming_client`、`test_stac_client`（`ctest -R "cli|exit_code|help"` 命中 15 个）。**既定测试模式 = popen 拉真 `sicnu_geo_rs_cli` 进程**（`test_cli_commands_json.cpp` 的 `runCli()`/`SICNU_TEST_CLI`），本轨道新测试沿用。
7. **MCP 侧入口**（WP-D 只读取证）：`src/agent/mcp_server.cpp:636-745` `toolName ==` 链（list_algorithms/search_algorithms/get_algorithm_schema/preflight_algorithm/execute_algorithm/list_operators/get_operator_schema/execute_operator/get_execution_status/scientific:agent_session/data-platform 工具族）；GUI 侧 = `src/app/workbench/command_defs.cpp` + CommandRegistry。
8. **评审 dossier 文件不存在**：PROJECT_REVIEW_DOSSIER_5.0.md / AUDIT_DOSSIER_ISSUES_747_760.md / PR_TRIAGE_REPORT_2026-09-16.md / docs/PARALLEL_TRACKS_10.md 均已不在工作树 → 按提示词预案以 open PR 正文为准（已通读 #1334-#1340 关键段）。
9. **CLI 3.0 usage 串散布**：cli_commands.cpp 内 12 处 `usage: ...` 内联串（:256/:391/:489/:583/:609/:728/:770/:819/:872/:905/:924/:952/:1033/:1873/:1925/:1986/:2261/:2415/:2529）+ 子命令 TU 各自的等价物。WP-B/WP-E 的统一出口落点。

## 6. 基线红绿分布

（基线构建完成后回填：`ctest -R "cli|exit_code|help" -j1` 实跑记录，区分"本来就红"与"本轨道引入"。）

## 7. 边界声明

白名单：`src/cli/**`、`tests/`（CLI 对应测试 + CMakeLists 注册 hunk）、`data/help/commands.json`（仅 CLI 轴面，GUI 词表轴让位 #1339）、`docs/`（仅退出码合同与错误信息标准两段追加）、`.planning/cli-surface-r4/**`。白名单外改动一律拒绝并记账。`src/cli/rs_pipeline_runner.cpp` 的 containment 语义只读不修改（#1334 领地）。
