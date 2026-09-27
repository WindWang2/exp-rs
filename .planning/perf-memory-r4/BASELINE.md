# BASELINE — Track 18: Performance & Memory R4（Phase 0 盘点，实测于 Linux 宿主）

- 盘点时间：2026-09-27（本地时区）
- 宿主：Linux 6.18.53-1-lts x64，16 核（AMD），62 GB RAM（盘点时可用 43 GB）
- 工具链：GCC 16.2.1 / ninja 1.13.2 / ccache（共享缓存 0.3 GB 冷启动）/ CMake
- 库：GDAL 3.13.3 "Iowa City"、Qt6（系统包）、jsoncpp
- 构建配置（本轨专用全新目录 `build-perf/`）：
  `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON -DCMAKE_{C,CXX}_COMPILER_LAUNCHER=ccache`
  编译纪律：`ninja -j2`（CMAKE_BUILD_PARALLEL_LEVEL=2），ctest `-j1`；RSS>70% 降级 -j1。
- perf/VTune：未安装 perf 事件采样许可（容器化内核限制待验证）；本轨采用**计时埋点 + 既有
  `sicnu::testing::perf` harness（tests/perf/perf_observatory.h，含 PeakRssTracker 外部轮询
  RSS、IoSnapshot、complexity ladder）**作为 profile 证据来源，理由：harness 已是仓库基准
  契约（obs_*.json 全部由它产出），复用即满足"不新造框架"门禁。

## 1. git 基线（实测）

- `origin/master` = `15e5c66b543ef3874cb929f17529ef456bd6c059`（PR #1333 合并点；与提示词
  写作时相同，**未前进**）。本地旧 master checkout 落后 159 提交（与本轨无关，worktree 从
  origin/master 新建）。
- 本轨分支：`hardening/r4-perf-memory`（worktree `/home/kevin/projects/rs-studio/exp-rs-perf-memory-r4`）。

## 2. 在途 PR 盘点与 file-overlap map（实测 5 个 open PR）

| PR | 标题 | 文件数 | 与本轨重叠判定 |
|---|---|---|---|
| #1334 | fix(security) MCP/CLI sandbox、PluginHost、keychain | 30 | 改 `tests/CMakeLists.txt`（本轨必改）——rebase 时需冲突复核；其余（agent/core/operators）与本轨无交集 |
| #1335 | fix: restore master build/CI (review P0) | 19 | 改 `tests/CMakeLists.txt`、`cmake/SicnuCatchAddTests.cmake`、顶层 CMakeLists；**其 PR 描述给出 Debian 全新构建 `all` 0 失败 target、ctest 7588 项**——证明 Linux 上 19 not_built 大多本就编译，win32 READINESS 快照的平台口径不适用 Linux；本轨接其"142 既有失败"中 READINESS 点名且 #1335/#1336/#1337/#1338 未覆盖的剩余项 |
| #1336 | fix(i18n,help,lab-pack) | 13 | i18n 漂移修复（data_manager_panel、schema_form_4 等）——与本轨 READINESS 面无直接交集 |
| #1337 | fix(workflow,agent) contracts converge | 25 | **改 `data/contracts/contract_graph.snap.json`、`data/help/*.json`**——#1335 描述中 contract_platform_9/command_contract_9/diagnostics_contract_9 的"快照过期"类失败正属此类；若 #1337 已修，本轨对其三项按"在途 PR 已覆盖"记账收窄，只修 #1337 未覆盖的漂移 |
| #1338 | fix(io,processing) atomic publish/writer/remote I/O | 29 | **直接改 `tests/test_fault_matrix.cpp`、`tests/test_io_paths.cpp`、`tests/test_io_remote_validator.cpp` + `src/geospatial/util/atomic_fs.*`、`raster_writer/vector_writer`**——#1335 描述的 Group 1（VectorWriter::create O_EXCL 暂存冲突，波及 io_atomic_failures）由 #1338 修；本轨处置 io 系 not_built 时以 Linux 实测为准，与 #1338 重叠的文件改动仅限本轨实测失败且 #1338 未修部分 |

**结论（#1335 归属判定）**：19 not_built 的根因主要是 **win32 快照宿主构建不全/平台缺失**，
不是 master 构建系统坏了（#1335 已把 Linux `all` 修到 0 失败 target）。本轨 WP-A 的实际工作 =
Linux 全新构建下逐项复测 19 项 → 已编译的直接跑（拉进默认面 + 记证据）→ 真实失败/环境缺失
的逐项处置。5 failed 中 3 项（platform/command/diagnostics contract 9）疑为快照漂移（#1337
在途），drift_projection_10 与 contract_projection_9 待 Linux 实测。

## 3. 评审材料

- `PROJECT_REVIEW_DOSSIER_5.0.md`、`AUDIT_DOSSIER_ISSUES_747_760.md`、
  `PR_TRIAGE_REPORT_2026-09-16.md`、`docs/PARALLEL_TRACKS_10.md`：**master 上已不存在**
  （实测 `ls` 全部 ENOENT）——按提示词以 git 历史与 open PR 描述为准（#1335 描述含 142 失败
  的完整八类分组，已作为本轨失败分类参照）。
- open issues：**0**（实测 `gh issue list` = 0）。

## 4. READINESS 复测与收口台账底稿

快照：`docs/verification/READINESS.md`（2026-09-16T07:02:56Z，SHA `d2868c7445`，
Host: **win32**）。Counts：compiled 26 / passed 21 / failed 5 / not_built 19 / timeout 1。
与提示词口径逐项一致，无漂移。

**not_built 19（win32 口径）→ Linux 处置计划**：

| # | capability | artifact | Linux 处置 |
|---|---|---|---|
| 1 | fuzz-operator-schemas | test_contract_fuzz_ops | 补编译+跑（tests/CMakeLists.txt:10652 无条件定义） |
| 2 | io-uri | test_io_uri | 同上（:377，sicnu_add_io_test） |
| 3 | io-paths | test_io_paths | 同上（:376）；注意 #1338 在途改动 |
| 4 | io-range-cache | test_io_range_cache | 同上（:431，SKIP_RETURN_CODE 77 = 内嵌 loopback HTTP server 启动失败时显式跳过） |
| 5 | io-remote-range | test_io_remote_range | 同上（:415，loopback server，无外网依赖） |
| 6 | io-remote-validator | test_io_remote_validator | 同上（:423）；#1338 在途 |
| 7 | io-atomic-failures | test_io_atomic_failures | 同上（:374）；#1335 Group1 的 VectorWriter O_EXCL 缺陷波及（#1338 在途修） |
| 8 | io-raster-contract | test_io_raster_contract | 同上（:357） |
| 9 | io-grid-descriptor | test_io_grid_descriptor | 同上（:468） |
| 10 | io-stac | test_io_stac | 同上（:361） |
| 11 | portable-fault-matrix | test_fault_matrix | 同上（:10364；POSIX 可用，win32 才缺） |
| 12 | sdk-ipc-contract | test_exprs_ipc | 同上（:9618，sicnu_add_sdk_test） |
| 13 | concurrency-stress | test_concurrency_stress | 同上（:10415，TIMEOUT 600 RUN_SERIAL） |
| 14 | posix-fault-injection | test_fault_injection | 同上（:3660，TIMEOUT 300；POSIX-only，Linux 应可跑） |
| 15 | worker-host-lifecycle | test_worker_host | 同上（:3578，TIMEOUT 300 RUN_SERIAL） |
| 16 | visual-cartography | test_mapspec | 同上（:775，qt_add_executable + jsoncpp） |
| 17 | bench-quality7 | benchmark_quality7 | 补编译+产出基准证据（:10456，gate 注释 `benchmark_quality7 --out benchmarks/quality7.json`） |
| 18 | bench-scale8 | benchmark_scale8 | 同上（:10673，`--out benchmarks/scale8`语境） |
| 19 | bench-contract9 | benchmark_contract9 | 同上（:10195，`--out` 证据件） |

**failed 5（win32 口径）**：test_contract_platform_9 / test_contract_projection_9 /
test_command_contract_9 / test_diagnostics_contract_9 / test_drift_projection_10
→ Linux 复跑后逐项根因分类（实现缺陷/契约漂移/环境依赖），以契约为真源修复；
platform/command/diagnostics 三项疑为快照漂移（对照 #1335 Group4 与 #1337 在途改动）。

**timeout 1**：fuzz-worker-ipc-splits / test_contract_fuzz_ipc —— **实测源码为纯进程内
bounded fuzz（tests/test_contract_fuzz_ipc.cpp，固定种子、≤512B 输入、300 迭代/种子，
无任何子进程/网络）**；win32 timeout 判定为慢宿主下Catch2 多用例注册+运行超默认时限。
Linux 处置：实跑 + 如实超时预算（TIMEOUT 已 300s），必要时按族拆分 RUN_SERIAL/标签，
不放松 fuzz 迭代数。

## 5. 远程开关与 skip 惯例（R4 核查项）

- `SICNU_TEST_REMOTE`：**全仓 0 命中（实测 rg）**——v1 所指开关不存在，如实记录。
- 既有门控惯例：io 远程三件 + range-cache 用 **`SKIP_RETURN_CODE 77` + 内嵌 loopback
  server**（`tests/support/http_range_server.{h,cpp}`、`http_stac_server.*`、`http_s3_server.h`）
  ——server 起不来返回 77 显式 skip，**不依赖外网**。本轨沿用该惯例（loopback + 77），
  不新造开关命名。

## 6. O(tile) 承诺点清单（rg 实测全集，WP-D 守护矩阵行来源）

文档面：
- `docs/adr/0073-large-raster-memory-policy-classification.md:16`（Streaming = O(tile) out-of-core 分级真源）
- `docs/adr/0089-post-classification-change.md:23`（block-wise 256 行，O(tile)）
- `docs/USER_GUIDE.md:1227`（六大时间算子 O(tile) 有界内存，默认 256×256，`T×tile×4B ≤ 256MiB` 收缩规则）
- `docs/processing/sar-domain.md:347`（SAR 流式 O(tile)）
- `docs/agent/tool-contracts.md:22`（cost_class "O(tile)" 契约字段）
- `docs/generated/help/operators.md:1325`（temporal_trend O(tile)）

实现面：
- `src/processing/framework/task_resource_budget.{h,cpp}`（Streaming=64MB / MultiPass=128MB 预算真源）
- `src/processing/gdal/gdal_multiband_block_stream.h:12`（BIP 窗口 per-pixel kernel O(tile)）
- `src/operators/framework/rs_operator.h:22`（Streaming 枚举承诺）
- 时序族：rs_temporal_trend_operator（六 float per-pixel state）、rs_temporal_decompose（O(tile×scenes) 如实申报）、rs_temporal_sar_fusion、rs_temporal_smooth
- 变化检测族：rs_change_primitives（256×256 tile 双缓冲）、rs_post_classification_change（两遍块计数）
- SAR 族：rs_sar_geocode / rs_sar_ratio / rs_sar_remove_topographic_phase
- 其它：rs_image_enhancement（#691 契约）、rs_topographic_correction、rs_band_tools、rs_terrain_analysis（halo-1 tiles）、rs_raster_spatial_operators（halo tiles）

**拟选守护矩阵（WP-D ≥3 条）**（固定输入 → PeakRssTracker 外部实测 ≤ 声明界）：
1. temporal tile streaming（`obs_temporal_tile_stream` 同族场景 + USER_GUIDE.md:1227 承诺，界=256MiB 收缩规则）
2. tiled inference（`obs_tiled_inference` 同族 + task_resource_budget Streaming 64MB 界 + ADR 0073）
3. change detection / post-classification 两遍块流（ADR 0089 O(tile) 承诺，界=tile×常数）

## 7. 基准面与 WP-E 扩展候选（实测 30 件）

构成：observatory 13 个 obs_*.json + data_manager_* 4 + model_tile_inference / model-runtime-4 /
model-runtime-9-cuda / quality7 / temporal_composite / spectral_index_streaming / recode /
qa_mask / majority_filter + ui-scale-5.0 系列 3（md+100k+10k）+ perf-observatory-baseline.md。

- obs_*.json 由 `tests/test_perf_observatory.cpp`（9 TEST_CASE）与 `tests/test_perf_io_observatory.cpp` 产出；
  harness 真源 `tests/perf/perf_observatory.h`（`measure`/`record`/`PeakRssTracker`/`Ladder`/
  `scaleFromEnv`/复杂度阶梯）；schema `sicnu-perf-observatory/1`（counts/environment/measurement/scale/structural/extra）。
- **WP-E ≥6 项扩展候选（全部为既有热路径的补测，复用同一 harness 与 schema）**：
  raster 直方图（rs_band_tools 直方图/分位流）、重投影 tile loop（GDAL warp 族）、
  变化检测（rs_change_primitives 256² 双缓冲路径）、时序合成（temporal_composite 既有
  bench 补 scale 档）、recode / qa_mask 族补档、majority_filter 补档。
  固定种子（writeSyntheticRaster 0x5EED 系）与规模档（Ladder 512/1024/2048），
  结果入 `benchmarks/observatory/obs_*.json` 并更新 perf-observatory-baseline.md 索引。

## 8. 边界声明（Scope 白名单）

允许触碰：`benchmarks/`；被 profile 点名的 `src/`（core/gui/app/operators/processing 最小侵入）；
`docs/verification/READINESS.md`；READINESS 收口涉及的 `tests/` 对应文件；`.planning/perf-memory-r4/`。
白名单外改动一律拒绝并记账。

## 9. 风险与在途 PR 触发器

- #1337/#1338 若先合并：master 将前进，本轨按铁律 rebase 复核（Phase 7 预留）；重叠文件
  （tests/CMakeLists.txt、io 测试）冲突时以"已合并内容为真源"收窄本轨改动。
- #1335 描述的 Group1（VectorWriter O_EXCL）若在 Linux 上使 io_atomic_failures 红：
  该根因属 #1338 范围，本轨不重复修实现，台账记录并指向 #1338；仅当 Linux 实测另有根因才动手。
