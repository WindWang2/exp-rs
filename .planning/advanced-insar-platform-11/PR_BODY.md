# Advanced InSAR Scientific Platform 11.0

> Local evidence only; no online CI dependency.

严谨轨道/DEM 去地形相位、解缠 provider 与多时相干涉网络。Base: `origin/master @ a5b11b7f`。Branch: `zcode/advanced-insar-platform-11`（worktree `../exp-rs-advanced-insar-platform-11`）。不 merge；不等待在线 CI。

## 与启动时并发对象的 dedupe / ownership

- 启动审计（2026-09-15，`.planning/advanced-insar-platform-11/BASELINE.md`）：快照中的
  #991/#992 已合入 master（a5b11b7f）；唯一 open PR #1008（spectral）与本 track 主写域
  **零文件交集**，共享集成文件均为 append-only 行级追加。
- 本地并发 worktree（cn-eo-product-physics / execution-runtime-convergence /
  geoai / spectral-intelligence / temporal-intelligence）均无 SAR 内核写域；
  cn-eo 的 GOAL 明确排除 "SAR algorithm kernels outside product decode"。
- Open issues #1001–#1007 全部为 io/workflow/dataset/georef/agent 域，逐条 dedupe 后
  记 OUT_OF_SCOPE（EVIDENCE.md），未据旧 ISSUES.md 开工。

## 实际交付（packages A–H → 模块/算子/测试）

| Package | 交付 | 关键文件 |
|---|---|---|
| A 轨道/基线真值 | `sar_baseline`：版本化场景真值 + 波长一致性 + 逐地面点严格基线（双星零多普勒）+ 高程模糊度 | `src/processing/algorithms/sar/sar_baseline.*` |
| B DEM/orbit 去地形相位 | `φ_topo = wrap(−4π(r_m−r_s)/λ)` 逐像元严格链 + 复数旋转去除；`rs:sar_remove_topographic_phase` | `sar_topographic_phase.*` + operator |
| C 共注册与相干质量 | 多尺度局部偏移场（格点 NCC + 中值 + 置信回退全局）+ warp；`rs:sar_coregister_local` | `sar_coregistration.*` + operator |
| D 解缠 provider | 通用外部可执行契约（bin 发现/args 模板/超时/取消/scratch RAII/输出校验，typed `UNWRAP_PROVIDER_*`）；builtin 保持默认 | `sar_unwrap_provider.*` + `rs:sar_unwrap` 扩展 |
| E 多时相 pair network | 约束 pair 图 + 并查集连通 + reference + fail-closed（断连/缺元数据/波长不一致）；triangle 相位闭合 QA；`rs:sar_pair_network` | `sar_pair_network.*` `sar_phase_closure.*` + operator |
| F 基础时序反演 | SBAS 式线性反演（per-pattern Cholesky 缓存 + 每像元参考分量 + pair 级权重）；`rs:sar_network_inversion`；诚实命名"线性小基线，非 PSI" | `sar_network_inversion.*` + operator |
| G 算子/知识/文档 | 4 新算子注册；sar.json 条目；13 个新 failure code 入封闭词表；43 个 algorithm_meta sidecar 再生；contract 快照再生；sar-domain.md §10–§14 中文文档 | 多文件 |
| H known-answer + scale | 独立 oracle 测试（解析闭式/测试本地 scan+bisect/闭合恒等式/构造真值/假 provider）；逻辑上限 typed refusal（512 场景/65536 pair/64 pair·200 epoch/128 pattern） | `tests/test_sar_*.cpp` ×8 + `tests/support/sar_fake_unwrap_provider.cpp` |

## 架构决定（详见 DECISIONS.md）

地形相位用严格双星 forwardRangeDoppler 链而非 B⊥ 近似（D-002）；provider 为零依赖通用
进程适配、SNAPHU 仅文档化预设（D-004）；pair 图断连默认拒绝、`allowDisconnected` 显式
降级（D-005）；反演 pair 级权重进正规方程、per-pixel 标量权重数学无效不收（D-006）；
错误码并入既有封闭词表（D-007，含复用 `WAVELENGTH_INCOMPATIBLE`）。

## 兼容性

- `rs:sar_unwrap` builtin 路径与新参数全部向后兼容（外部 provider 需显式 opt-in）；
  `sar_orbit.h`/`sar_insar.h` 零改动；词表 append-only。
- 能力条目/algorithm_meta/contract 快照全部由官方工具再生并被各自 drift gate 字节级
  锚定（`test_algorithm_meta_drift` / `test_contract_platform_9` /
  `test_capability_drift` 全绿）。

## 本地证据（无在线 CI 依赖；完整清单见 EVIDENCE.md / TEST_MATRIX.md）

- 构建：dev-default preset（MSVC 14.38 + Ninja -j2 + vcpkg），首全量 1506 targets
  exit 0；测试 `QT_QPA_PLATFORM=offscreen`、`ctest -j1`。
- **双验证**：修复后两遍连续 —— ① 34/34 InSAR targeted 组 PASS + capability/snapshot/
  contract-graph gates PASS；② 34/34 再跑 + meta/snapshot/contract/capability 四 gate
  再 PASS。回归：test_sar_orbit / test_sar_insar / test_sar_operators 抽组全绿。
- 交叉验证 oracle 独立性：解析赤道闭式、测试本地 scan+bisect（与实现不同算法）、
  闭合相位代数恒等式、构造位移表、确定性假 provider（ok/crash/truncated/nan/hang）。

## Review findings（独立对抗 review，无 P0）

- P1 warp 符号文档写反 → 已更正（头文件 + 域文档，与实现/函数契约/测试一致）。
- P2 ×6：Windows sidecar rename、GRID_CRS_MISSING/DEM_CRS_MISMATCH 入词表、
  反演 master/slave 指引更正、topo 诊断栅格 NaN、内存预算 48/28 B/px 更正。
- P3：行级取消探针、offset 栅格节点中心 GT、死代码/注释/docs typo 清理。
- 全部 14 项 finding 有 disposition（REVIEW_LOG.md）；修复后双验证如上。

## 范围外最小修复（PR 顶部披露）

master @ a5b11b7f 在 MSVC 上不可构建、两个平台 gate 上游即红；为取得本 track 的
gate 证据做了最小数据/编译修复（均非 SAR 域）：

1. `pipeline_run_coordinator.cpp`：Q_OS_WIN 分支缺 `<fcntl.h>`（c5d4aafe 引入，
   _O_WRONLY/_O_BINARY 未定义 → sicnu_workflow MSVC 构建破坏）。
2. `data_platform_tools.cpp`：`BenchmarkService`/`benchmarkRunStatusToString` 未限定
   `sicnu::experiment`（a5b11b7f 引入 → sicnu_agent MSVC 构建破坏）。
3. capability 数据：gaofen/hj/zy3_import 三重复制去重（保留 preprocess 富集版）；
   补 7 个已注册算子的缺失条目；删 4 个上游不再生成的 rs-temporal-* sidecar。
   修复后 `contract_inventory` findings=0，三个 drift gate 全绿。

## Known limitations（诚实边界）

- 非 PSI：无大气分离/PS 选择/时空滤波；`rs:sar_network_inversion` 为线性小基线。
- 共注册为平移场模型（非仿射/DEM warp）；解缠 builtin 非 residue-aware（全局语义
  由外部 provider 承担）。
- 屏幕级 B⊥ 为图筛选指标（master 中时刻 nadir），非逐像元产品。
- 逻辑上限（typed refusal）：网络 512 场景/65536 pair；反演 64 pair/200 epoch/
  128 NaN-pattern；unwrap builtin 2 GiB 平面预算。

## Follow-ups（不在本 PR）

- 大气改正与 PS/时序滤波（PSI 完整化）；仿射/range-Doppler 精配准。
- contract_inventory 的 gaofen/zy3/hj 上游条目来源治理（cn-products 数据面）。
- SNAPHU 实机集成验证（本 PR 只交付通用契约 + 假 provider 全分支测试）。
