# PLAN — Track 18 perf-memory R4

分支 `hardening/r4-perf-memory`（自 origin/master=15e5c66b5），worktree 隔离，
构建 `build-perf/`（Release/Ninja/ccache/ENABLE_TESTS=ON），`ninja -j2`、`ctest -j1`。

## 判定原则（来自 Phase 0 实测）

1. win32 READINESS 快照（d2868c744）不直接约束 Linux；**以本机全新构建实测为处置真源**，
   台账逐项记录 win32 状态 → Linux 实测 → 三态结论。
2. 与在途 PR 重叠的失败（#1337 契约快照、#1338 io-writer O_EXCL）**不重复修**：
   Linux 复现证据 + 台账指向在途 PR，计为"在途覆盖"（书面豁免子类，依据=PR diff 实读）。
3. 其余失败逐项根因分类后修复；一提交一类根因；关键验证双跑。
4. 快照刷新机制（`SICNU_CONTRACT_SNAPSHOT_WRITE`）仅当本轨拥有该文件时使用——
   当前 data/contracts、data/help 归 #1336/#1337，本轨不碰。

## Phase → WP 映射与验收

- **Phase 1（WP-A 前半）**：build-perf 全量编译完成后，`ctest -N` 实测 19 not_built 的
  Linux 注册态；io 系 9 项 + fuzz_ops 逐项跑并处置；台账 READINESS_CLOSURE.md 开账。
- **Phase 2（WP-A 后半 + WP-B）**：其余 not_built 10 项（fault_matrix/exprs_ipc/
  concurrency_stress/fault_injection/worker_host/mapspec/3 bench）+ 5 failed 复跑分类 +
  timeout 1 本地化实测。
- **Phase 3（WP-C+WP-D）**：profile 证据（obs harness 计时 + 复杂度阶梯）→ ≥8 处最小优化
  （含 ≥2 拷贝消除，digest 一致）+ 缓存正确性 ≥2；内存守护 ≥3 条（PeakRssTracker 外部实测，
  界值注释引 ADR 0073 / USER_GUIDE:1227 / ADR 0089 / task_resource_budget）。
- **Phase 4（WP-E+WP-F）**：基线扩展 ≥6 项（obs schema 对齐，真实 obs 文件名引用）+
  READINESS.md 快照更新 + 全量回归双跑 + DECISIONS.md 四规则。
- **Phase 5**：独立只读 review（1 subagent 槽位）→ P0/P1 修复 → REVIEW_LOG.md。
- **Phase 6**：EVIDENCE.md 归档（两轮日志）、整洁提交、PR（不轮询 CI）。

## 提交计划（≥18 原子提交的预算）

WP-A 处置约 6-8 个（一族一提交：CMake 注册/测试修复/豁免文档）、WP-B 3-5、
WP-C 8-10（一处一提交）、WP-D 2-3、WP-E 2-3、WP-F 2、工件 1-2。

## 账本

`.goal-loop-ledger.md`（worktree 根，随分支提交——沿用 #1336-#1338 仓库惯例）；
每轮记录：轮次 | 改动 | 验证命令 | 结果 | 本轮 tokens | 累计 tokens。
