# Recon — RS14-16-scene-suitability

Track: `agent/rs14-scene-suitability-assessor`
Baseline: `origin/master` @ `4f6632e1f6bb41f90729800d0c7bf569ff34edb3` (PR #1145 merged).
Worktree: `/home/kevin/projects/rs-studio/exp-rs-wt-rs14-suitability`
Recon date: 2026-09-21. Dynamic dedup performed against open PRs (none) and open issues (#1146–#1187) at start.

## 1. 目标重述

在实验规划前回答"这批数据适不适合完成这个目标"。输入 = 一个实验目标（任务族 + 需求）+ 一批候选数据（scene/collection/dataset），输出 = 结构化、versioned、machine-readable 的适用性报告：每个 criterion 给出 `suitable / marginal / unsuitable / unknown` 四态 + evidence + 缺口清单；支持 task profiles（classification/change/phenology/object detection 等）；教学模式解释"为什么不适合"；Agent 模式供 plan 前机器筛选。**不自动搜索/下载数据**。

## 2. 已有能力（可复用，不重复实现）

### 2.1 词汇与模式（必须遵循的既有惯例）
| 能力 | 位置 | 事实 |
|---|---|---|
| Result/Diagnostic | `src/data/data_result.h` | `Result<T>` + `Diagnostic{code,message,severity}`，全仓统一错误词汇 |
| AuditVerdict | `src/dataset/dataset_types.h:211` | `enum class AuditVerdict { Pass, Warn, Fail, Unknown }` —— QA 专用，本 track 需要**不同**的四态 lattice（suitable/marginal/unsuitable/unknown），不复用 AuditVerdict（语义不同：QA 是质量审计，suitability 是目标匹配） |
| Report 模式 | `src/dataset/dataset_qa_report.h` | `kXxxSerializationVersion=1` 常量、category struct{verdict,summary,evidence,diagnostics}、`toJson()`/`static Result<T> fromJson`（严格 schema_version，foreign version → typed failure）、`buildXxxReport(Inputs)` 纯函数、"Does not I/O"、"never Pass on partial evidence" |
| 严格分页 | `src/data/query_cursor.h` + `DatasetStore::samplesPageCursor` | keyset cursor，`kMaxPageSize=500`，`dataset.cursor_mismatch` fail-closed |
| 任务族 | `src/dataset/dataset_types.h:223` | `BenchmarkTaskFamily { Classification, Segmentation, ChangeDetection, ObjectDetection, Regression, TemporalPrediction, SpectralMatching }` —— **直接复用**，不发明第二套任务枚举。phenology 在此枚举中无独立值 → profile 层以额外 profile key 表达（见 plan） |
| 网格兼容 | `src/data/raster_grid_compat.h` | `RasterGrid` + `GridCompatReport compareGrids/compareStructures` —— grid compatibility criterion 的既有事实源，直接调用 |
| 波段语义 | `src/data/band_role.h` | `BandRole { Blue,Green,Red,RedEdge,NIR,…,QA,SceneClassification }` |
| 资产结构 | `src/data/data_asset.h` | `RasterStructure{width,height,bandCount,crsWkt,geoTransform,extent,bands[]}`、`SpatialExtent`、acquisitionTime |
| STAC 元数据 | `src/geospatial/stac/stac_mapper.h` | `StacItem{datetime,platform,cloudCover,gsd,bbox,assets,modality,…}`（jsoncpp/Qt-free，`sicnu::geo`）—— cloud cover / GSD 的既有来源 |
| 数据集清单 | `src/dataset/dataset_manifest.h` | `DatasetSchema{modality,sensor,crs,bandRoles,resolutionX/Y}`、`spatialExtent()`、`temporalExtent()`、labelSchema、statistics/quality/provenance |
| 数据集只读面 | `src/dataset/dataset_store.h` | `sampleCount`、`samplesPageCursor`、`facetDistribution`、`facetCrossCounts`、`qualitySummary`、`latestQaReport` —— **只读调用允许** |
| 样本行视图 | `src/dataset/sample_catalog.h` | `SampleCatalogRow{classCode,sensor,region,modality,year,splitRole,quality,hasPseudoLabel}` |
| 组成统计 | `src/dataset/dataset_quality.h` | `DatasetComposition` + `imbalanceFindings`（caller 组装 flat rows，不物化全数据集） |
| Agent 工具 | `src/agent/data_platform_tools.h` + `src/agent/tool_catalog/` | 薄 stateless adapter 模式（"No business logic lives here"）、`ToolProvider` 接口、`toOpenAiToolDefinition()/toMcpToolDefinition()` |

### 2.2 模块分层
`sicnu_data`（Qt6::Core+GDAL）→ `sicnu_dataset`（依赖 data 的 Result 词汇，headers Qt-only，SQLite PRIVATE）→ `sicnu_experiment`。`sicnu_geo/stac` Qt-free（jsoncpp）。

## 3. 缺口（本 track 要新建的）

1. **无任何 suitability 概念**：`grep -ri suitab src` 仅命中无关注释。没有"目标→数据匹配"的结构化评估器。
2. 无 `SuitabilityGoal` / `SuitabilityCriterion` / `SuitabilityReport` 类型。
3. 无 task→requirement 的 profile 层（BenchmarkTaskFamily 有枚举但无"该任务需要什么数据"的需求模板）。
4. 无跨 asset+dataset 的聚合评估服务。
5. 无 teaching-mode 叙事解释器、无 `suitability:` agent 工具。

## 4. 不做什么（Scope 边界）

- **不修改** DatasetStore/DataManager 的任何写路径；只调用其公开读接口（通过 provider 接口隔离）。
- 不处理 #1184（agent 10k 截断）——本模块自己的 provider 带显式 cap，不依赖 dataset: 工具面。
- 不做数据搜索/下载/推荐排序引擎；缺口清单是事实陈述，不是市场。
- 不重复 RasterGrid/Leakage/QA 报告的实现；只投影与调用。
- 不发明第二套任务枚举、verdict 序列化方式或 Result 类型。
- 不修复避让清单中的任何 open issue（#1146–#1187）；发现则记录 observed。
- 不构建 GUI 面板（GUI 未来只应调用 core service）。

## 5. 架构决策（要点，详见 plan.md）

- 新模块 `src/suitability/`，namespace `sicnu::suitability`，target `sicnu_suitability`（静态库，headers Qt-only，依赖 `Sicnu::data` + `Sicnu::dataset`）。
- **输入 DTO 自包含**：`SceneCandidate`（单景/资产投影）+ `DatasetFacts`（数据集统计投影）为纯值对象；从 `RasterStructure`/`DatasetManifest`/`StacItem` 的桥接是薄适配函数（adapter），assessor 本体只见 DTO。
- **规模查询走 provider 接口**：`SuitabilityDataProvider`（fake/in-memory 用于 TDD；`StoreDataProvider` 薄适配 DatasetStore 读面，带显式 caps）。
- 四态 lattice `SuitabilityLevel{Suitable,Marginal,Unsuitable,Unknown}`，聚合规则"unknown 不许升格为 suitable；unsuitable 传染 overall"。
- criteria 为纯函数：`assess<Something>(goal, facts, profile) -> SuitabilityCriterion`。
- schema versioned：`kSuitabilityReportSerializationVersion = 1`。

## 6. 风险

| 风险 | 缓解 |
|---|---|
| 与 19 个并行 track 撞车（尤其 rs14-agent-benchmark 可能也要评估数据） | DTO/provider 边界 + integration.md 接线点；PR 前动态去重 |
| QGIS 重构建拖垮机器 | 只 build `sicnu_suitability` + 单测 target；-j1；新模块纯 C++ 可不链 QGIS |
| metadata 缺失导致假 suitable | Slice G 对抗性：unknown fail-closed 语义先行测试 |
| StacItem 依赖 jsoncpp | STAC 桥接放在适配 cpp，核心 DTO 不含 jsoncpp 类型 |

## 7. 与 open issues 去重矩阵

| Issue 区域 | 关系 | 处置 |
|---|---|---|
| #1184 agent 10k 截断 | 本模块规模查询同样怕截断 | 自带 capped provider + `truncated` 标志显式上报，不触碰 #1184 本身 |
| #1151/#1187 capability mirror 红灯 | 无关（agent 工具目录） | 不触碰 |
| #1171 ghost facets / #1173 prune / #1172 runIds | DatasetStore 数据正确性 | Suitability 只投影读取；observed，不修 |
| #1161 CatalogRecordStore resyncKeys | 资产查找顺序 bug | provider fake 不受影响；real adapter 记录 known-limitation |
| #1163 VSI ETag / #1162 mirror | geospatial 缓存缺陷 | 无关 |
| #1150/#1183 光谱检测 | 无关 | 不触碰 |
| #1177 perf baseline 丢失 | 无关（observatory 文档） | 本 track 性能预算写入自己的 plan.md |

## 8. 与其他 19 tracks 的接口原则

- 消费方未来接 `sicnu_suitability` 的公开头 + JSON schema；不依赖本 track 的内部文件布局。
- 对"另一 track 尚未存在的类型"（如统一的 ExperimentGoal 类型）：先定义最小 `SuitabilityGoal` DTO + fake provider，在 `docs/integration.md` 写清未来接线点，不复制对方实现。
- 中央文件只动三处且保持最小 delta：`settings.json`/顶层 `CMakeLists.txt`（若 add_subdirectory 必须）、`tests/CMakeLists.txt`（新测试 target）、`.gitignore`（planning allow-list 一行）。
