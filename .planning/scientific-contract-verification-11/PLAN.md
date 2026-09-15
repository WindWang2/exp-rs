# PLAN — scientific-contract-verification-11

Baseline `a5b11b7f`。Oracle 见 GOAL.md；本文件是执行序，账本是 `.goal-loop-ledger.md`（worktree 根）。

## 设计核心（Package→落点）

| WP | 落点（write scope 内） | 交付 |
|---|---|---|
| A contract census 2.0 | `src/contracts/census_11.{h,cpp}` | live census：对 live registry 全部 first-party 算子（rs:/io:/otb:/opencv:/cartography:）产出统一记录：scientific contract（rs:）/ contract 缺席 + exemption、determinism stamp 来源（explicit override vs default fallback）、sidecar grade、seed policy。导出机器可读 JSON。 |
| B determinism truth | census + `src/operators/**` 最小点改 + sidecar JSON 修正 | 消灭"静默 tolerance 兜底"：每个 bit_exact/tolerance 声明要么有显式代码覆盖、要么有 data/contracts exemption 记录（含理由+证据锚）。执行证据：抽代表算子实际运行两次比对（bit_exact→byte-identical；tolerance→数值界）。 |
| C metamorphic oracle | `tests/test_metamorphic_oracle_11.cpp` | 按族不变量：spectral index scale-invariance 扩展（offset/band-reorder）、warp 平移不变性、zonal stats 平移/缩放不变性、NoData 单调性（NoData 输入→NoData/排除输出）、reproject 恒等 CRS 往返、temporal 时间平移不变性、filter 线性族叠加分解。含 mutation kill 断言（人为注入 6+ 种变异，测试必须抓到）。 |
| D numeric reference lane | `tests/test_numeric_reference_11.cpp` | 独立闭式/高精度 oracle（long double 或独立公式实现，写在测试内）：NDVI/SAVI、辐射定标 DN→radiance→TOA→bt、Welford、直方图分位数、SAR sigma0、谐波拟合单频情形、双线性重采样解析情形、zonal 均值/方差。不复用被测实现函数。 |
| E failure/cancel/atomic lane | `tests/test_failure_contract_11.cpp` | 对 contract 声明的 refusal_codes/atomic_publication/cancellation_granularity 做存在性与行为验证：corrupt input→typed refusal 且无半成品文件；cancel→typed cancel error 且无 partial publish；磁盘满模拟（bounded 空间注入/只写句柄关闭）→ 失败清理；read-only 源。 |
| F cross-surface drift | `src/contracts/graph_assembly.cpp` 扩展 + 快照再生成 + `tests/test_cross_surface_drift_11.cpp` | descriptor↔sidecar↔help↔agent capabilities↔LabSpec↔contract graph 六面一致性：help operators json 每个 operator id 可解析进 live registry；agent capability knowledge ↔ sidecar；LabSpec ↔ registry（10.0 已有部分）；补 help 与 CLI command refs 面。快照字节 gate。 |
| G verification ladder 11 | `scripts/verification_ladder.py` | capability-aware lanes：L0 compile-guards/L1 unit-core/L2 contract-known/L3 integration-io/L4 portability/L5 stress-lifecycle/L6 visual-optional/L7 benchmarks；每 lane 检测本机依赖，缺依赖→显式 skipped(not-built: reason)，不引用在线 CI。输出 JSON 报告入 data/contracts 或 planning。 |
| H readiness report | `scripts/collect_readiness.py` + `docs/verification/READINESS.{json,md}` | 机器可读 evidence index、baseline delta（10.0 skipped=3/not_built=16 → 11.0 实况）、pre-existing failure 分类（含 master 上 test_capability_drift 2 例）。 |

## Phase 序

- **P0**（done）审计、worktree、planning 文件、configure+全库栈构建启动。
- **P1** census 数据模型 + determinism 债务清单落盘（先只读审计不改算子）；census JSON + snapshot gate 测试骨架。
- **P2** metamorphic lane 第一块（spectral/warp/zonal）+ 独立 reference lane 第一块；determinism 点修第一批（证据驱动）。
- **P3** metamorphic 第二块（NoData/CRS/time）+ failure/cancel/atomic lane；determinism 点修第二批 + exemption 记录。
- **P4** cross-surface drift 扩展 + 快照再生 + surface 接线；docs/verification 同步。
- **P5** ladder capability-aware 化 + 实际执行 L0–L5（本机可执行子集），证据固化。
- **P6** readiness 报告 + known-answer 补齐 + 全量 targeted sweep 两遍。
- **P7** subagent #2 adversarial review + P0/P1 修复。
- **P8** 双验证、rebase、push、PR。

## 构建策略（Windows, MSVC, Ninja, -j2）

configure 配方见 BASELINE；构建目标：库栈（sicnu_contracts/geospatial/data/processing/jobs/task_center/agent/operators/workflow_runtime + vendored qgis_core）+ 按名测试可执行文件（不全量 550 个 test exe）。每 60s 记录 CPU/RSS（tasklist/PowerShell），load 不可测。
