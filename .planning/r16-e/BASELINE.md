# R16-E Baseline — 帮助/i18n + 构建/离线交付

- **执行时间**：2026-10-17（UTC）
- **基线 SHA**：`46791bc65c6f552ebbcf723bb91892736d56f319`（= prompt 参考值；`git fetch origin --prune` 后 origin/master 无新提交）
- **宿主**：ZCode（无 `/teamwork-preview`、`/goal-loop` 命令）。语义等价替代：Agent 工具并行子代理（双轨隔离 worktree）= Teamwork 双执行单元；本目录 `GOAL_LOOP_LEDGER.md` = Oracle 台账；两轮未参与实现的 Reviewer 子代理 = 对抗评审。无任何虚构命令输出。
- **环境**：40 核 / 62GB（RSS 约束按 70%≈44GB 监控）；cmake 3.30.5、Ninja、flock 可用；**无 ccache**；磁盘空闲 346GB。构建硬上限 `-j4`（`CMAKE_BUILD_PARALLEL_LEVEL=4`），CTest `-j1`。R16 跨 Prompt 共享构建锁：`/home/kevin/project/exp-rs-r16-build.lock`。
- **工作树**：
  - 轨道 09：`/home/kevin/project/exp-rs-r16-e-t09`（分支 `hardening/r16-e-t09-help-i18n`）
  - 轨道 10：`/home/kevin/project/exp-rs-r16-e-t10`（分支 `hardening/r16-e-t10-build-packaging`）
  - 集成：`/home/kevin/project/exp-rs-r16-e-integration`（分支 `hardening/r16-e-help-delivery`）
  - 同仓已存在 R16-A/B/C 等其它 Prompt 的 worktree 与分支（`hardening/r16-a-*`、`r16-b-*`、`r16-c-*`、`hardening/planner-*`、`agent/*` 等）——本任务全部只读，不清理、不重置。
- **轻量构建配置**（复制自 r16-c 已验证组合，各 worktree 自有 build dir）：Ninja + `CMAKE_BUILD_TYPE=Debug` + `SICNU_BUILD_OTB=OFF` + `SICNU_EMBED_PYTHON=OFF` + `SICNU_ENABLE_OPENVINO=OFF` + `SICNU_ENABLE_TENSORRT=OFF` + `SICNU_VENDOR_GDAL=OFF` + `SICNU_WITH_ONNX_RUNTIME=OFF` + `SICNU_TOLERANT_TEST_DISCOVERY=ON`。估算 -j4 全量 ≈ 70–120 分钟（无 ccache），因此只做目标性构建。

## 阶段 0 现场核验结论（每条都有代码/命令证据，非转述）

| 项 | 结论 | 证据 |
|---|---|---|
| #1490 concurrency 组 | **已修复于 master**（勿重修） | `.github/workflows/ci.yml:9-16`：push 按 SHA 分组注释引用 #1490 与 r13-i 证据 JSON |
| #1495 clang-tidy continue-on-error | **已修复于 master** | `ci.yml:106-113`：#706/#1495 注释、阻断式步骤 |
| #1494 孤儿测试 | **已修复于 master** | `tests/CMakeLists.txt:12767-12778` 注册 `test_challenger_m1_2_harness`；且 `scripts/check_test_registration.py`（R13-I WP-04）已是 census 门禁 |
| #1468 Windows `_exit` | **已修复于 master** | `tests/sicnu_test_main.cpp:53-63` "Return normally; never _exit" |
| #1468 mini_cog_server recv 无超时 | **已修复于 master** | `tests/support/mini_cog_server.h:102-104` `SO_RCVTIMEO=30s` |
| #1468 netcdf find_library 无守卫 | **已修复于 master** | `tests/CMakeLists.txt:438-449` 带 configure-time 失败消息守卫 |
| #1465 terminology 去重后缀未应用 | **已修复于 master**（待回归复测） | `src/help/terminology_provider.cpp:183-200` `assignedIds` 计算并传递 |
| #1489 lupdate 只扫 src/app | **部分修复** | `CMakeLists.txt:973` 已扩至 13 个目录（app/gui/core/processing/analysis/agent/agent_ops/workflow/experiment_studio/teaching_admin/teaching/operators/ui），但仍有第一方目录未覆盖（src/cli、src/sdk、src/plugins、src/native、src/dataset、src/geospatial、src/experiment、src/runtime 等是否含 tr() 需扫描证明）；`.ts` 新鲜度门禁是否存在待查 |
| #1466 整数目标错误类型 | 已有失败处理（prompt 调研，待轨道内复核 suitability_goal.cpp） | — |
| #1491 CI 无完成结果 / test_io_vector_contract:352 | **状态待本地复现**（CI 侧 #1490 修复后应有改善；本地需跑该测试验证 :352 是否仍失败） | — |

## 开放 PR 冲突矩阵（对 R16-E 文件归属）

| PR | 标题 | 与 T09 交集 | 与 T10 交集 |
|---|---|---|---|
| #1513 (r15-B, draft) | 空间数据链路/持久化 | 无 | 无（src/** only + .planning） |
| #1514 (r15-A) | 科学算法 | 无 | 无 |
| #1515 (r15-C) | 模型运行时 | 无 | `tests/CMakeLists.txt`（追加测试注册） |
| #1516 (r15-D) | Pi/MCP/Workflow | 无 | 无 |
| #1517 (r15-E) | 桌面教学/帮助质量 | **`src/help/operator_help_provider.cpp`（新增 keyParameters↔schema 校验）、`data/help/operators/data_io.json`、`data/help/operators/optical.json`（keyParameters 数据修正）、`tests/CMakeLists.txt`** | `tests/CMakeLists.txt` |

处置：T09 将 `operator_help_provider.cpp` 与 `data/help/operators/{data_io,optical}.json` 视为**只读**（#1517 未合并，不重构其工作）；帮助数据审计聚焦其余 data/help 文件（commands.json / concepts.json / diagnostics.json / workbenches.json / operators 其余文件），代码审计聚焦 help 模块其余文件。`tests/CMakeLists.txt` 仅允许最小增量注册（追加行，不重排），与 #1515/#1517 的追加式改动手工合并无语义冲突。
