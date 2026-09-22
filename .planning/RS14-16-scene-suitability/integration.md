# Integration — RS14 scene-suitability 接线点

本模块是纯科学核心叶子（`sicnu_suitability`，namespace `sicnu::suitability`），无 I/O。与其他 track / 未来消费方的全部接线点如下。**接入原则：消费方依赖公开头 + versioned JSON schema，不依赖内部文件布局；不复制本模块实现。**

## 1. Agent / MCP 消费（已接线）

- 工具：`suitability:assess`、`suitability:profiles`（`data_platform_tools.cpp` 注册，adapter 在 `src/suitability/suitability_agent_adapter.{h,cpp}`，可在不链 agent 库的情况下单测）。
- 契约：结构坏请求 → throw（MCP isError）；内容无效 → `{valid:false, diagnostics}`；成功 → `{valid:true, overall_level, report(完整 v1 JSON), report_digest, gaps, teaching}`。调用方可用 `SuitabilityReport::fromJson(report) + contentDigest` 复核（可调用且可验证）。
- goal / scenes 均为 JSON 文档字符串，schema 见 `suitability_goal.h` / `scene_candidate.h`（各带 `schema_version: 1`，严格解析）。
- **capability mirror 纳入前提**：#1151/#1187 修复后，把 `suitability:` 加入 `tests/test_capability_drift.cpp` 的 `kDataPlatformPrefixes` 并在 `data/agent/capabilities/` 写两个工具的 knowledge 条目。在此之前刻意不动（避免加重红灯）。

## 2. Experiment 侧接线（未来）

- `Experiment.objective`（自由文本）与 `BenchmarkDefinition.taskFamily` → `SuitabilityGoal` 的映射建议：taskFamily 直接映射；objective 不做 NLP——由调用方（GUI 向导或 agent）把目标形式化为 goal 字段（AOI/窗口/波段/样本数）。`phenology` profile = `TemporalPrediction` 族 + `profileKey:"phenology"`。
- 落点：实验创建向导在**提交前**调 `SuitabilityAssessor::assess`（或 StoreDataProvider 路径），把 report JSON + digest 存进实验的 provenance/evidence 字段（report 是 self-contained value object，可直接嵌入）。
- 去重边界：不写 experiment store、不复制 EvaluationProtocol——只消费。

## 3. GUI 向导（未来）

- GUI 只调 core service（`SuitabilityAssessor::assess` / `teachingExplanation`），不自行实现业务逻辑；教学叙事逐 criterion 一段，可直接进结果面板。
- 资产侧投影：`sceneCandidateFromRasterStructure(id, state, structure, hints)`（`scene_candidate.h`）；STAC 路线建议在 import 层把 `StacItem.gsd / cloudCover / datetime` 填进 hints（`gsd_m` / `cloud_cover_percent` / `acquisition_time_utc`）——这是米制 GSD 与云量的权威来源，assessor 核心绝不猜。

## 4. DatasetStore 只读投影

- `StoreDataProvider`（只读：sampleCount / facetDistribution / qualitySummary / manifest 投影），caps 见 `FactsLimits`；截断语义见 progress.md 决策记录（"(other)" 折叠桶 → factsTruncated；部分计数冒充总数是 bug）。
- 扩展位：新 facet → 在 provider 投影处加一行；不改 DatasetStore。
- 明确不做：不修 #1184（agent 10k 截断）——本模块 cap 体系独立且显式上报。

## 5. 与其他 19 个并发 track 的边界

- 提供方：`SuitabilityReport` v1 JSON + digest 是稳定出口；criteria id 目录（`spatial.*` / `temporal.*` / `quality.*` / `spectral.*` / `labels.*` / `grid.*` / `model.*` / `uncertainty.*`）与 gap id 前缀是稳定契约，消费方可依赖。
- 需要方：任务族词汇只复用 `sicnu::dataset::BenchmarkTaskFamily`；网格判断只复用 `sicnu::data::compareGrids`；错误词汇只用 `Result`/`Diagnostic`——本模块不发明平行真相源，也要求消费方不绕过这些事实源。
- 若另一 track 提供统一的"实验目标"结构化类型，接线点是 `SuitabilityGoal` 的字段映射函数（新增一个 adapter 头即可），不需要改 assessor 核心。
