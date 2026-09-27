# DECISIONS — Track 18 perf-memory R4

## 规则 1：READINESS 收口处置判据（何时豁免）

每项处置必须落到三态之一，且有可复核证据：
- **passed**：本机 `build-perf` 全新构建 + `ctest -R <name>`（或 bench `--out`）绿，双跑。
- **failed→fixed**：Linux 实测红 + 根因分类（实现缺陷/契约漂移/环境依赖）+ 最小修复 +
  双跑绿。修复以契约为真源，禁止放松断言换绿。
- **书面豁免**（仅三类正当）：
  a) **平台缺失**——POSIX-only 测试在非 POSIX 宿主、或依赖本机不存在的硬件/运行时，
     须引源码证据（`#ifdef`/CMake 条件）与 ladder 的 host-reason 惯例；
  b) **在途 PR 覆盖**——open PR（实测 #1336/#1337/#1338）已携带同根因修复且文件归其所有，
     须引 PR diff 实读 + 本机复现输出，本轨不重复修以免制造必然冲突；
  c) **宿主局限**——如 ladder 注释中 fuzz_ipc 在 Windows 命名管道模拟的病态慢；
     Linux 实测绿则此项转为 passed，不适用豁免。
  豁免必须写入 READINESS_CLOSURE.md 逐项条目：根因、证据、指向。

## 规则 2：O(tile) 内存界声明规则

- 每条守护测试的界值 = **承诺出处可追溯**（注释引用文件:行）：ADR 0073（Streaming 分级）、
  `docs/USER_GUIDE.md:1227`（时序族 `T×tile×4B ≤ 256MiB` 收缩规则）、
  `src/processing/framework/task_resource_budget.cpp`（Streaming 64MB / MultiPass 128MB 预算档）、
  ADR 0089（变化检测 block-wise）。
- 测量一律 **外部化**：`sicnu::testing::perf::PeakRssTracker`（独立线程轮询进程 RSS），
  禁止用实现自申报的内存数自证；守护在 `-j1`、`RUN_SERIAL` 下跑，避免测量污染。
- 超界处置：先修实现（流化/分块不到位的最小修复）；确属界声明过严才书面修正界，
  依据入本文件追加小节，并同步承诺文档（若该文档归本轨）。

## 规则 3：profile 证据门槛（何时允许性能改动）

- 每处优化提交前必须有：①计时/复杂度证据（obs harness `measure()` 数字或独立计时埋点，
  同机同负载、双轮取稳定值）；②定位（点名函数与行）；③最小修复；④前后计时对比入
  EVIDENCE.md；⑤行为不变证据（既有 digest/等价 oracle，或新增 digest 对比）。
- 无证据的"顺手优化"一律不做；优化不许改变公共契约（digest 零容差）。
- 拷贝消除以 digest 一致为行为不变判据；涉及缓存的改动必须同步补失效正确性测试。

## 规则 4：基线固定规则（WP-E）

- 新基线必须：复用 `tests/perf/perf_observatory.h`（`measure`/`record`/`Ladder`/
  `scaleFromEnv`）与 `sicnu-perf-observatory/1` schema；固定种子（writeSyntheticRaster
  系数）与规模档；产物落 `benchmarks/observatory/obs_*.json`（文件名引用既有 13 件
  实测名单的风格，不虚构）；`perf-observatory-baseline.md` 索引同步。
- 数值是记录性的（供未来对比），结构门（counts/复杂度指数/工作集单位）是门禁性的。
- 基线运行环境如实记录（Release、本机、并行负载声明）。

## 附：本轨边界（白名单外不动）

`benchmarks/`、profile 点名的 `src/` 热路径、`docs/verification/READINESS.*`、
READINESS 收口涉及的 `tests/` 文件、`.planning/perf-memory-r4/`。
data/contracts、data/help 归 #1336/#1337；atomic_fs/writer 归 #1338——本轨只在
Linux 实测失败且非其在途修复对象时才最小介入并记账。
