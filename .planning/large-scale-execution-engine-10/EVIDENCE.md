# EVIDENCE — large-scale-execution-engine-10

证据政策：每条能力断言映射到本地命令 + 退出码，或显式标注 not-executed。

## Phase 0（基线，2026-09-14）

| 断言 | 验证 | 结果 |
|---|---|---|
| 基线 SHA | `git rev-parse origin/master` | `7d78059d1a6d316d606656759a506d17bc5e3b55` |
| worktree 建立 | `git -C ../exp-rs-large-scale-execution-engine-10 log --oneline -1` | `7d78059d1a Merge pull request #958 ...` |
| planning 白名单生效 | `git add -n .planning/large-scale-execution-engine-10/GOAL.md` | `add '.planning/.../GOAL.md'`；`git check-ignore -q` exit=1 |
| OPEN PR 去重 | `gh pr list --state all --limit 40` | #973/#974/#975 OPEN；#974 chunk_plan 在 `src/geospatial/fabric/`（无文件级冲突） |
| 执行平面文件在位 | `wc -l src/processing/framework/task_center.*` 等 | task_center 857+4356；job_engine 283+1120；local_worker_pool 202+681；execution_plane 285+435 |
| 9.0 测试基线 | `.planning/execution-concurrency-lifecycle-9/FINAL_REPORT.md` | test_execution_plane_9 865 assertions/14 cases（该 track 本地证据，master 上套件存在） |

## 构建证据（每 Phase 追加）

### Phase 0/1 构建

- not-executed yet（Phase 1 实现后统一构建并记录 configure/build 退出码）。

## OUT_OF_SCOPE

- #970 `.planning` 白名单过程缺陷（多 track 共享问题）——本 track 只加自身条目。
- #960 offline gate 裸 bool 竞态（src/data/offline_mode，data plane 所有权）——记录待 data 轨处理；若 Phase 4 集成中发现与本 track 接线直接相关再评估。
- review/findings F-OPS-1/2/3（operators 语义缺陷）——operators 轨所有权。

## 预算计量

| Phase | 时间戳(UTC) | 工具调用 | 触及文件 |
|---|---|---|---|
| 0 | 2026-09-14 | ~20 | .gitignore + 10 planning 文件 |

## Phase 1-3 构建与测试证据（2026-09-14，worktree build-dev Debug -j2）

| 证据 | 命令 | 结果 |
|---|---|---|
| configure | `cmake --preset dev-default` | exit 0 |
| 构建（runtime 链） | `cmake --build build-dev --target test_chunk_graph test_external_memory_10 -j2` | exit 0 |
| 构建（TaskCenter/算子链） | `--target test_large_scale_execution_10 test_execution_fingerprint / test_rs_operator test_atomic_algorithm_adapter test_model_tasks` | exit 0 |
| ChunkGraph 契约 | `./tests/test_chunk_graph` | 342 assertions / 25 cases 全绿 |
| 外存层 | `./tests/test_external_memory_10` | 44 assertions / 8 cases 全绿 |
| fingerprint 契约（env pins） | `./tests/test_execution_fingerprint` | 64 assertions / 17 cases 全绿 |
| scale/failure 默认档 | `./tests/test_large_scale_execution_10` | 6218 assertions / 5 cases 全绿 |
| scale 压测档 | `SICNU_LSEE10_STRESS=1 ./tests/test_large_scale_execution_10 "[scale]"` | exit 0（10^6 逻辑 tile fan-out join + scratch storm + cache 规模） |
| 算子契约 | test_rs_operator / test_atomic_algorithm_adapter | 625/16、34/3 全绿 |
| 模型任务（#971 NMS 取消） | test_model_tasks | 1253 assertions / 10 cases 全绿 |
| 回归（TaskCenter/worker） | test_execution_plane_9 / test_worker_host | 构建后执行（见下） |

修复轮次：preflight 命名空间（92abf063d9）；scratch Deleter 未接线 + tile/checkpoint 头 digest 覆盖缺口（fda52869a5）；scale 测试 live 计数口径（f056e617f4）；NMS 测试 fixture disjoint 化（80023e290f）。

## 补充回归证据

| 证据 | 命令 | 结果 |
|---|---|---|
| ep9 执行平面回归（TaskCenter/协调器/checkpoint） | `./tests/test_execution_plane_9` | 883 assertions 全绿 |
| worker 宿主/池回归（首跑 8 失败 = sicnu_worker 未构建，构建后复跑） | `cmake --build build-dev --target sicnu_worker` + `PATH=$PWD:$PATH ./tests/test_worker_host` | 61 assertions / 13 cases 全绿 |
| 10^6 压测复跑（落盘日志） | `SICNU_LSEE10_STRESS=1 ./tests/test_large_scale_execution_10 "[scale]"` | exit 0 / 6208 assertions 全绿（/tmp/lsee10-stress.log） |

## OUT_OF_SCOPE（补充）

- grid-indexed NMS（#971 的 O(n·k) 加速）——取消注入已闭环可中断性；精确等价的 grid 桶实现属独立 PR，见 PR_BODY follow-ups。

| 核心回归补全 | test_job_engine / test_task_center | 446/34、382/32 全绿 |

## Phase 8 最终验证（final HEAD @ review-remediation 后，2026-09-14）

| 套件 | 结果 |
|---|---|
| test_chunk_graph | 353 assertions / 30 cases 全绿 |
| test_external_memory_10 | 50 assertions / 8 cases 全绿 |
| test_execution_fingerprint | 64 assertions / 17 cases 全绿 |
| test_preflight（tilePlan 契约 + F-A-3 拒绝） | 118 assertions / 9 cases 全绿 |
| test_model_tasks（#971 取消语义） | 1254 assertions / 10 cases 全绿 |
| test_large_scale_execution_10 | 6218 assertions / 5 cases 全绿 |
| test_large_scale_execution_10 [scale] ×3 压测档 | exit 0（10^6 逻辑 tile） |
| test_worker_host | 61 assertions / 13 cases 全绿 |
| test_task_center | 382 assertions / 32 cases 全绿 |
| test_job_engine | 446 assertions / 34 cases 全绿 |
| test_execution_plane_9 | 883 assertions 全绿 |
| `git diff --check` | clean |
| 冲突标记扫描（新增文件） | 0 命中 |
| secret 扫描（新增文件） | 0 命中 |
| 文档存在性断言（goal-template 引用面） | 0 MISSING |

复验捕获并修复 F-M-1（测试内嵌套 lambda 悬垂捕获 → stack smashing，P0 级测试缺陷，生产代码无涉），修复后 8+3 连跑全绿。

## PR

- URL：https://github.com/WindWang2/exp-rs/pull/980
- 状态：OPEN，不由本 track merge（track 契约）
- 分支：zcode/large-scale-execution-engine-10 → master（rebase 于 origin/master @ 7d78059d1a，无冲突）
