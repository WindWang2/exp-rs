# PLAN — Advanced InSAR Platform 11.0 执行计划

基线：`origin/master = a5b11b7f`；branch `zcode/advanced-insar-platform-11`；worktree `../exp-rs-advanced-insar-platform-11`。

## Phase → 交付映射（commit 粒度）

- **Phase 0（本 commit）**：规划件 + .gitignore 白名单 + GOAL 存档。无业务代码。
- **Phase 1 — 基础契约/authority（A）**：`sar_baseline.h/.cpp`：
  - `InSarSceneTruth{acquisitionUtcSec, orbit(OrbitSegment), wavelengthUm}` + `validateSceneTruth`
    （轨道契约校验、λ>0 有限、UTC 有限）；
  - `buildPairTruth(master, slave)`：时间有序（slave−master 允许负？统一 master 早于 slave →
    命名 master/slave 语义；违反 → typed refusal `PAIR_ORDER_INVERTED`?——保守：允许任意顺序，
    输出 temporalDays 带符号）、波长一致性（相对差 > 1e-9 → `WAVELENGTH_MISMATCH`）、
    同轨道时间基（无共同覆盖 → `ORBIT_EPOCH_MISMATCH`）；
  - `pairBaseline(pairTruth, los)`：调 interferometricBaseline + `pairPerpBaselineAtDEM`（可选）。
  - 已知答案测试 `tests/test_sar_baseline.cpp`（圆轨道解析真值）。
- **Phase 2 — 核心第一块（B+C）**：
  - `sar_topographic_phase.h/.cpp`：`topographicPhase(demGrid+heights, orbitM, orbitS, λ, GT, CRS)`
    逐像元 `forwardRangeDoppler` → φ_topo；`removeTopographicPhase`（wrap 相减，NaN 传播）；
    streaming 行窗；元数据缺失 typed refusal（`TOPO_PHASE_METADATA_MISSING`）。
    `rs:sar_remove_topographic_phase` 算子（DEM 与干涉图网格关系校验：同 CRS、DEM 覆盖干涉网格）。
  - `sar_coregistration.h/.cpp`：`estimateOffsetField`（格点 patch NCC + 亚像元 + 置信）+
    `medianFilterOffsets` + `warpComplexByOffsetField`（分块双线性）；NaN/边界语义。
    `rs:sar_coregister_local` 算子（slave 重采样 + offset/conf 产品）。
- **Phase 3 — 核心第二块（D+E）**：
  - `sar_unwrap_provider.h/.cpp`：provider registry（内置 builtin 注册；外部可执行 provider：
    bin 发现 param→env `SICNU_SAR_UNWRAP_SNAPHU_BIN`、args 模板占位符、QProcess、超时、
    取消 kill、临时文件 `.tmp~` 原子、stderr 捕获、输出尺寸/有限性校验、typed refusals
    `UNWRAP_PROVIDER_*` 家族）；`rs:sar_unwrap` schema/实现扩展（builtin 默认不变）。
  - `sar_pair_network.h/.cpp`：场景真值数组 → 约束过滤（maxTemporalDays/maxPerpM/minPerpM）→
    graph（all_pairs/参数化）→ 连通分量（并查集）→ reference 选择 → 输出 pair 表 + 分量 +
    fail-closed 语义（元数据缺失/波长不一致 = refusal；断连默认 refusal、`allowDisconnected`
    显式降级为带标注结果）。`rs:sar_pair_network` 算子。
  - `sar_phase_closure.h/.cpp`：wrapped 三角闭合核 + 栈 RMS。
- **Phase 4 — 反演 + surface（F+G）**：
  - `sar_network_inversion.h/.cpp`：G 矩阵（npairs×nepochs）→ 加权正规方程（权重=coherence²）
    → 一次 Cholesky per NaN-pattern（pattern 缓存，上限→typed refusal 或 intersect 策略）；
    per-epoch 位移 + 线速度（LS 斜率）+ 残差 RMS + 时间相干；非参考分量 NaN + 计数。
    `rs:sar_network_inversion` 算子（stack 输入 = pair 顺序对齐的 unwrapped 栅格目录/数组）。
  - surface 同步 integration commit：4 算子注册、`data/agent/capabilities/sar.json`、
    `data/processing/algorithm_meta/capability/rs-sar-*.json` ×4、kFailureModeCodes、
    harness_error 分类、contract_inventory 再生快照、docs/processing/sar-domain.md §10+、
    `pi/knowledge/capability-sar.md`（若存在）。
- **Phase 5 — 硬化**：取消（所有长核 cancelProbe 贯通）、原子写/失败清理路径测试、
  Unicode 路径 fixture、只读源、NoData 边界、pattern 上限/epoch 上限 refusal、
  内存上限（构造级 + 估算断言）；PERFORMANCE.md 回填。
- **Phase 6 — E2E/known-answer 全链**：`tests/test_sar_platform11.cpp`：
  合成轨道+DEM+位移场 → 干涉 → topo 去除 → 解缠 → pair 网络 → 反演速度恢复（解析容差）；
  全部 drift gates 重跑。
- **Phase 7 — 对抗 review**：subagent #2 只读全 diff review + 主 agent 全 diff 自审；P0/P1 修复。
- **Phase 8 — 双验证 + PR**：targeted gates 连续两遍；rebase；push；`gh pr create`（PR_BODY.md）。

## 构建命令（硬约束）

```bash
CMAKE_BUILD_PARALLEL_LEVEL=2 cmake --preset dev-default   # worktree 根
cmake --build build-dev --target <targets> -j2            # 压力高 → -j1
QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev -R sar -j1
```

## 每个 Phase 的完成定义

业务 diff + kernel/operator 测试绿 + planning 件回填 + 原子 commit + rebase origin/master。
