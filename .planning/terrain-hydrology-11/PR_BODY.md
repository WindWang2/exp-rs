## F16 · Terrain, Hydrology & Visibility Analytics 11.0

DEM 水文、可视域、太阳地形与流式地形分析平台。Baseline: `origin/master @ a5b11b7f10`
（prompt 生成时的 `ebcafb4d02` 已过期；启动审计以新事实为准，D18/D19 已合入）。
**Local evidence only; no online CI dependency.**

### 与并行工作的 dedupe / ownership

- 启动时唯一 open PR #1008（radiometric/spectral workbench）无 terrain 业务文件交集；
  共享集成文件（`spatial_tool.cpp`、若干 CMakeLists、`.gitignore`）只做 append-only 接线。
- open issues #1001–#1007（io/workflow/dataset/georef 域）与本 track 无重叠，未动。
- 新出现的 `zcode/spectral-intelligence-11` 远端分支（未合并）亦无 terrain 文件交集。

### 实际交付（对照 prompt 的 work packages）

- **A · DEM conditioning**：`rs:terrain_flow product=flat_resolve` — epsilon 梯度
  priority-flood 平地解析（epsilon = max(relief·2⁻²⁰, 1e-6)，单调不降，未用时与 `fill`
  字节兼容）；关闭 `foundation-5.md` 记录的既有债务。
- **B · flow**：`product=flow_direction_inf` — Tarboton (1997) D∞（8 三角面最陡下降 + 楔形
  约束；平面上精确复现梯度方位角；pit 规则 + 锥面退化几何的 D8 回退，保证非 pit 单元
  必有下游）；单接收者累积（质量恒等式精确；分数加权拆分记为 follow-up）。
- **C · watershed/stream**：`product=stream_network`（threshold 提取 + Strahler 级序 +
  连通性统计 + 可选 Strahler link 折线，GDAL 像元中心地图坐标）；`product=outlets`
  （D8 方向 0 单元掩膜 + JSON 列表）。
- **D · viewshed/horizon**：新 `rs:terrain_viewshed`（viewshed | cumulative）— 环扫
  R3 族视域（确定性 permissive merge）、观察/目标高度、半径、逐 LOS 对精确的
  地球曲率/折射校正（地理坐标系拒绝）；NoData 不透明。
- **E · solar**：新 `rs:terrain_solar`（shadow_duration | hillshade_series）— 加权太阳
  轨迹下的平行光线阴影时长（逐样本精确，1° 方位角分扇；阴影不做曲率，v1 已声明），
  轨迹可显式给出或按日期/纬度以本地太阳时生成（Spencer 1971 级数，声明 ±1°）；
  hillshade 序列复用既有 hillshade 核。
- **F · landform**：新 `rs:terrain_landform`（tpi_multiscale | landform_class |
  geomorphon）— 积分图方窗多尺度 TPI（中心排除）、Weiss (2001) 六类地形位置
  （复用 TerrainAnalysis::slope）、J&S 地貌形态元三元模式（无歧义 7 类 v1 子集）。
- **G · surface**：TerrainDialog 按产品族路由到域算子（观察点/阈值/日期-纬度控件）；
  只读 agent 工具 `spatial:terrain_profile`、`spatial:terrain_viewshed_inspect`
  （复用同一内核，2²⁴ 单元快速检查预算）。
- **H · synthetic truth**：`tests/synthetic_terrain_dem.h` 闭式工厂（plane/cone/pit/
  ridge/channel/NoData collar）；全部 oracle 独立（手推盒均值、逆图递归累积、
  天文表值、闭式曲率地平线）。

### 架构决定（详见 DECISIONS.md）

不重复造轮子：master 已有 fill/D8/累积/流域与曲率等核，本 track 只向下一可验证缺口
深化。新核与既有 terrain 家族同契约（NoData 障碍、确定性 tie-break、全帧内存策略）。
新增 3 个 operator 而非塞进 `rs:terrain_analysis`：后者硬连 3×3 halo 流式路径，全帧
核强塞会分叉其执行模型（D8 有完整论证）。

### 兼容性

- 既有 4 个 `rs:terrain_flow` product 与 11 个 `rs:terrain_analysis` product 行为不变
  （`fill` 在无洼地/平地输入上字节等价，含单测）。
- 新 operator 为纯增量注册；capability 覆盖保持集合相等（135/135），monotone floor
  pin 未改。

### 本地验证（全部可复现，命令见 EVIDENCE/TEST_MATRIX）

- 9 个套件、最终树上连续两遍全绿：test_terrain_hydrology（12/1023）、
  test_terrain_viewshed（6/938）、test_terrain_solar（5/62）、test_terrain_landform
  （5/198）、test_terrain_analytics_e2e（9/1076，含取消/预算/注册表 E2E）、
  test_terrain_agent_tools（3/52，含 Unicode 路径）、test_terrain_foundation5 +
  test_terrain（既有回归，27/1717）。
- `test_capability_knowledge` 12/1210 全绿；`capability_knowledge_tool gen-meta`
  （135 sidecars）与 `gen-pages`（zero diff）exit 0。
- 规模证据：env opt-in（`SICNU_TERRAIN_SCALE_TESTS=ON`）hermetic scale 测试，
  2048²（4.19M 单元）填洼+D∞+累积+单调性不变量 6.99 s 完成；默认 gate 不含。
- 资源：全帧 operator `SICNU_TERRAIN_MAX_CELLS` fail-closed（默认 2²⁸ 单元），
  动态 `estimatedRamBytes` 按帧数/尺度数计算；取消一律以 `Cancelled` 浮出，
  失败不留部分输出（GdalStreamingOutput abandon）。

### 独立 review（只读子代理，完整 origin/master...HEAD diff）

结论 P0=0、P1=1、P2=3、P3=8。**P1/P2 全部修复**；P3 修复 7 项、1 项（共享 LoS
march 去重）recorded 为 follow-up；逐条处置见 `REVIEW_LOG.md`。修复后重跑
gen-meta/pages 并两遍复验全部相关 gate。科学预检由 reviewer 独立确认
（D∞ 代数、阴影 carry 递推、Spencer 系数、TPI 积分图、侧车一致性、oracle 独立性）。

### 范围外修复（master 既有问题，对照实验证明）

- `sicnu_agent` 自 D19 合入后在 master 上编译失败（`data_platform_tools.cpp` 未限定
  `BenchmarkService`）— 一行 `using namespace sicnu::experiment;` 修复。
- capability 覆盖缺口：7 个先前 merge 的 spectral/temporal operator 无 sidecar；
  `data/agent/capabilities/preprocess.json` 中 3 条重复 CN-import 条目阻塞
  `gen-meta`；stale 的 ace/temporal sidecar 由官方工具再生。对照实验：去掉本
  track 的 sidecar 时该套件 4 个失败（全部既有），加上后 0 失败。

### 已知限制 / follow-ups

- D∞ 累积为单接收者（无分数加权拆分）；阴影无曲率/折射；TPI 方窗；geomorphon 为
  10 类的无歧义 7 类子集；全帧核（预算内）而非外存分块水流 — 均已文档化。
- follow-up：共享 nearest-cell LoS march 与 cell-pair 解析助手；循环 TPI 窗；
  完整 10 类 J&S 表；外存分块水流路由。

### Planning / evidence artifacts

`.planning/terrain-hydrology-11/`（GOAL/PLAN/BASELINE/DECISIONS/CURRENT_ARCHITECTURE/
CAPABILITY_MATRIX/PARALLEL_OWNERSHIP/TEST_MATRIX/PERFORMANCE/EVIDENCE/REVIEW_LOG/
PR_BODY）与 worktree 根 `.goal-loop-ledger.md`。
