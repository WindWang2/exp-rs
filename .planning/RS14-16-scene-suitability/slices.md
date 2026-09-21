# Slices — RS14-16-scene-suitability

每 slice：RED（先写失败测试，确认因"缺能力"失败）→ GREEN（最小实现）→ REFACTOR → narrow test → adversarial 复核 → commit → progress.md 更新。

## Slice A — schema/status lattice（`suitability_level` / `suitability_types` / `suitability_report`）
- `SuitabilityLevel{Suitable,Marginal,Unsuitable,Unknown}` + 字符串互转（round-trip）。
- 严重度秩 `suitabilitySeverityRank`；`aggregateLevels`（Unknown>Suitable、Marginal>Unknown、Unsuitable>Marginal）。
- `SuitabilityGap` / `SuitabilityCriterion`（applicable 标志、notes、gaps、diagnostics）toJson/fromJson 严格 round-trip（含 foreign version、缺失字段 typed failure）。
- `SuitabilityReport`：criteria 规范序、overallLevel() 跳过 not-applicable、allGaps() 排序去重、contentDigest() 确定性（同内容两次构建 digest 相同；criterion 顺序不同→聚合语义相同但 digest 规范化后相同）、serialization v1。
- RED 测试文件：`tests/test_suitability_core.cpp`。
- 交付：`src/suitability/{suitability_level.h/.cpp, suitability_types.h/.cpp, suitability_report.h/.cpp, CMakeLists.txt}` + 顶层 CMake 一行。

## Slice B — spatial coverage/resolution criteria（+ `SceneCandidate` DTO）
- `SceneCandidate` 值对象 + `sceneCandidateFromRasterStructure(id, state, RasterStructure, ...)` 适配器（gsd 从 geoTransform 派生，extent 透传）。
- `assessSpatialCoverage`：同 CRS 才计算（异 CRS→Unknown+note，不做重投影）；矩形 union（坐标压缩）；valid AOI 缺失→Unknown+note；覆盖率阈值→Suitable/Marginal/Unsuitable；无可用场景（state!=Ready）→Unknown/Unsuitable 语义按全部无效→Unsuitable（有 AOI 无任何数据）。
- `assessResolution`：gsd ∈ [min,max]；未知 gsd→unknown 贡献；空可用集→Unknown；全部超限→Unsuitable；部分→Marginal。
- RED 测试：`tests/test_suitability_spatial.cpp`。

## Slice C — spectral/quality criteria
- `assessSpectralBands`：requiredBandRoles ⊆ ⋃bandRoles（scene ∪ facts）；每缺一个→gap `band.missing.<role>`；bandRoles 全空→Unknown。
- `assessCloudCover`：maxCloudCover<0→not applicable（profile 可判 N/A）；未知云量场景→unknown 贡献；全部超限→Unsuitable；部分超限→Marginal（in-range 子集证据）；全部未知→Unknown。
- RED 测试：`tests/test_suitability_spectral.cpp`。

## Slice D — temporal criteria
- `assessTemporalCoverage`（窗口交集；无窗口→Unknown+note）、`assessTemporalDensity`（窗口内数量 vs profile minScenesInWindow）、`assessTemporalSeasonality`（月份→气象季映射+note 声明半球假设；requiredSeasons 缺失→gap `season.missing.<s>`；样本季相 facts 优先融合）。
- DatasetFacts 有时间分布时与 scene 时间融合投影。
- RED 测试：`tests/test_suitability_temporal.cpp`。

## Slice E — labels/model/grid criteria + provider
- `SuitabilityDataProvider` 接口 + `FactsLimits` + `InMemoryDataProvider`（fake）。
- `assessLabelAvailability`：requireLabels=false→not applicable；缺 schema→Unsuitable gap；requiredClasses 缺失→gap；sampleCount<minSamples→gap；pseudo-label 存在且政策禁止→Marginal+gap；counts 未知→Unknown。
- `assessGridCompatibility`：<2 grid→Unknown+note（或 1 个 grid→not applicable）；≥2：compareGrids 抽样对（cap 200，超限 note+uncertainty）；blocking 失配→profile 严格 ? Unsuitable : Marginal。
- `assessModelCompatibility`：goal 无 model→not applicable；model 波段/分辨率/模态 vs 数据；不满足→gap。
- `StoreDataProvider`：只读适配 DatasetStore（sampleCount/facetDistribution/qualitySummary/latestQaReport → DatasetFacts），LIMIT caps，截断上报。RED 测试：`tests/test_suitability_labels.cpp`、`tests/test_suitability_store_provider.cpp`（真实 temp-file DatasetStore，参照 test_dataset_core.cpp 构造模式）。

## Slice F — task profiles
- 内置 profile 表：classification/segmentation/change_detection/object_detection/regression/temporal_prediction/spectral_matching + `phenology` 附加 profile（TemporalPrediction+profileKey）。
- `resolveRequirements`：显式 goal 值覆盖 profile 默认；未知 profileKey→`suitability.profile_unknown`；profile 产出 not-applicable 集合（如 spectral_matching 的 model）。
- 每任务族一个端到端 mini-scenario（学生故事）：classification 缺 NIR→报告含 `band.missing.nir` gap；phenology 冬季缺测→`season.missing.winter`。
- RED 测试：`tests/test_suitability_profiles.cpp`。

## Slice G — adversarial：unknown/missing metadata + caps
- 缺元数据矩阵：无 gsd/无时间/无云量/无波段/无 schema → 对应 criterion Unknown，**overall 永远不许 Suitable**。
- 截断：factsTruncated=true→uncertainty source + overall 降格（Suitable+截断→至少 Marginal? 否——截断→uncriterion Marginal，overall 相应）。
- 恶意/异常输入：极端 gsd=1e9、负云量、空 bandRoles 字符串、QJson 深嵌套 evidence（限制 evidence 由内部生成，fromJson 深度由 QJson 默认限制兜底+测试）。
- 确定性重放：同输入两次 assess → digest 相同；HashMap 遍历不稳定→序列化前排序（回归测试锁死）。
- RED 测试：`tests/test_suitability_adversarial.cpp`（多数用例在 A–F 已绿灯行为上补强 oracle，少数驱动修复）。

## Slice H — teaching mode + agent tool + integration docs
- `teachingExplanation(report)`：逐 criterion 人读段落；"为什么这组影像不适合本实验"叙事；teaching 结论与 JSON level 一致性测试。
- Agent 工具：`suitability:assess`（+`suitability:explain`）薄 adapter，走 data_platform_tools 消费点注册（Slice H 前先 recon 消费点）；machine-readable JSON in/out；"能调用也能验证"——输出即 versioned report。
- `docs/integration.md`（track 内）：未来接线点（ExperimentGoal、GUI 向导、capability mirror）。
- RED 测试：`tests/test_suitability_teaching.cpp`、agent 工具注册/分发测试。

## 尾部 gates
- Deep review（两轮）→ 修复 P0/P1/P2 → targeted regression → 动态去重（fetch + master/issues/PR 重查）→ union rebase（若 master 前移）→ PR。
- Track DoD：①本科生端到端场景（F 的 mini-scenario）②agent machine-readable（H）③单一事实源（只调 compareGrids/复用 BenchmarkTaskFamily/Result 词汇）④typed 失败全路径 ⑤离线（测试无网络）⑥预算上界（caps+抽样+坐标压缩）⑦动态去重记录。
