# Plan — Scene/Dataset Suitability Assessor (`sicnu_suitability`)

Track: `agent/rs14-scene-suitability-assessor` · Baseline `4f6632e1f6` · 2026-09-21

## 1. Problem statement

在投入计算/实验之前，没有任何结构化机制回答"这批数据适不适合完成这个目标"。现有能力（DatasetQaReport、composition、grid compat、STAC 元数据）各自回答单点问题，但没有把**目标需求**与**数据事实**对接起来的评估层。学生靠"点按钮试"，Agent 无机器可读依据。

## 2. User stories

**本科生（教学视角）**：我在实验向导里选定"土地覆盖分类"目标和一个数据集/一组影像，系统在跑实验前告诉我：每个维度（覆盖/分辨率/波段/时相/标签/网格/模型）是 suitable / marginal / unsuitable / unknown，用一条我能读懂的解释说明"为什么这组影像不适合本实验"，并给出缺口清单（缺 NIR、季相缺冬季、样本量不足 500 等），让我先修数据或改目标。

**AI Agent（规划视角）**：我在制定实验计划前调用 `suitability:assess` 工具（输入 goal JSON + datasetVersionId / scenes），得到 versioned JSON 报告（overall level、per-criterion level + evidence + gaps、digest），据此选择换数据、调目标或继续。报告可持久化、可回放、可比较。

## 3. Architecture

```
┌────────────────────────── 消费方（未来） ──────────────────────────┐
│  GUI 向导（只调 core service）   Agent MCP 工具（薄 adapter）        │
└──────────────┬───────────────────────────────┬───────────────────┘
               │            SuitabilityReport (JSON, v1)            │
┌──────────────▼───────────────────────────────▼───────────────────┐
│  sicnu::suitability（新模块 src/suitability，纯计算，无 I/O）        │
│  SuitabilityAssessor::assess(Inputs) -> Result<SuitabilityReport>  │
│   ├─ resolveRequirements(goal, profiles)  （task profile → 需求）   │
│   ├─ criteria_* 纯函数 × 9（spatial/temporal/spectral/quality/     │
│   │   labels/grid/model/uncertainty…）                             │
│   ├─ SuitabilityReport（聚合 + 序列化 + digest + teaching 叙事）     │
│   └─ SuitabilityDataProvider 接口（规模查询的唯一通道，带 caps）      │
│        ├─ InMemoryDataProvider（fake，TDD 用）                      │
│        └─ StoreDataProvider（只读适配 DatasetStore 读面，capped）    │
└──────────┬──────────────────────────┬─────────────────────────────┘
     复用 Sicnu::data（Result/Diagnostic/RasterGrid/SpatialExtent）
     复用 Sicnu::dataset（BenchmarkTaskFamily/DatasetManifest 词汇）
     桥接（adapter，非依赖）：RasterStructure→SceneCandidate、
     StacItem→SceneCandidate、DatasetManifest→DatasetFacts
```

原则：单一事实源（网格兼容只调用 `compareGrids`，任务族只复用 `BenchmarkTaskFamily`，错误词汇只用 `Result/Diagnostic`）；assessor 核心只见自包含 DTO（`SceneCandidate`/`DatasetFacts`），不 import STAC/GDAL 头；不修改 DatasetStore/DataManager 写路径。

## 4. Public API / data schema

### 4.1 状态 lattice

```cpp
enum class SuitabilityLevel { Suitable, Marginal, Unsuitable, Unknown };
// 严重度秩：Suitable(0) < Unknown(1) < Marginal(2) < Unsuitable(3)
// overall = 各 applicable criterion 的最大秩。Unknown 排在 Suitable 之上：
// "partial evidence never Suitable"（对齐 DatasetQaReport 的既有语义）。
// criterion 另有 applicable 标志：profile+goal 判定"不适用"的 criterion
// 不参与聚合，但仍在报告中列出（evidence.status="not_applicable"）。
// 注意：缺元数据 ≠ 不适用 —— 缺数据永远 → Unknown，不许降级为 N/A。
```

不复用 `AuditVerdict`（QA 语义 vs 目标匹配语义不同，避免第二语义污染）。

### 4.2 核心类型

```cpp
struct SuitabilityGap {
    QString id;            // 稳定机器码，如 "band.missing.nir"、"samples.below_minimum"
    QString criterionId;   // 反向引用
    QString description;   // 人读
    QJsonObject evidence;  // required vs found
    // toJson/fromJson
};

struct SuitabilityCriterion {
    QString id;            // "spatial.coverage", "temporal.seasonality", ...
    bool applicable = true;
    SuitabilityLevel level = SuitabilityLevel::Unknown;
    QString summary;
    QJsonObject evidence;
    QStringList notes;     // 诚实声明：什么没法查（假设、截断、CRS 跳过）
    QVector<SuitabilityGap> gaps;
    QVector<Diagnostic> diagnostics;
    // toJson/fromJson（严格）
};

class SuitabilityReport {
    // subject 描述（datasetVersionId / scene ids / goalDigest）
    // QVector<SuitabilityCriterion>（规范 id 序 → 确定性 JSON）
    // overallLevel()、allGaps()、contentDigest()（QCryptographicHash over 规范 JSON）
    // toJson() / static Result<SuitabilityReport> fromJson（kSuitabilityReportSerializationVersion=1）
};
```

### 4.3 Goal 与 profiles

```cpp
struct SuitabilityGoal {           // versioned JSON 可序列化
    BenchmarkTaskFamily taskFamily = Classification;
    QString profileKey;            // ""=taskFamily 默认；"phenology" 等额外 profile
    // 显式需求（显式值覆盖 profile 默认）
    SpatialExtent aoi;  QString aoiCrsWkt;          // aoi.valid=false → coverage Unknown
    bool hasTimeWindow=false; QDateTime windowStartUtc, windowEndUtc;
    QStringList requiredSeasons;                    // {"spring"..}
    double minGsd=0, maxGsd=0;                      // 0=不限
    QStringList requiredBandRoles;                  // band_role.h 词汇
    double maxCloudCoverPercent=-1;                 // <0=不限
    qint64 minSamples=0; QStringList requiredClasses; bool requireLabels=false;
    ModelRequirements model;                        // optional（hasModel）
    double minCoverageFraction=0.0;                 // 0 → profile 默认
};
struct ResolvedRequirements { /* goal 显式值 ∪ profile 默认值；不可变 */ };
Result<ResolvedRequirements> resolveRequirements(const SuitabilityGoal&);
// typed failure: "suitability.profile_unknown"（未知 profileKey）、"suitability.goal_invalid"
```

内置 profiles（每任务族 1 个 + phenology 附加 profile）：
`classification`（minCoverage=0.95、requireLabels、minSamples=200）、`segmentation`、`change_detection`（双时相需求：≥2 个时相簇、季节配对提示）、`object_detection`（小 GSD 上限、样本即目标数）、`phenology`（TemporalPrediction+profileKey="phenology"：多时相密度、整年覆盖、季节均匀）、`regression`、`temporal_prediction`、`spectral_matching`（波段需求优先）。profile 只填**默认值**，goal 显式值永远优先；profile 不做计算。

### 4.4 数据事实 DTO 与 provider

```cpp
struct SceneCandidate {           // 资产/单景投影（自包含值对象）
    QString id; AssetState state;                       // state!=Ready → 不计入可用集
    QString crsWkt; SpatialExtent extent;
    std::optional<double> gsdM; std::optional<QDateTime> acquisitionTimeUtc;
    std::optional<double> cloudCoverPercent;
    QStringList bandRoles; int bandCount=0;             // bandRoles 空 → spectral Unknown
    std::optional<RasterGrid> grid;                     // grid compat 用
    QString modality; QJsonObject provenance;           // 透传 evidence
};
struct DatasetFacts {             // 数据集统计投影（全部 -1 = unknown）
    QString datasetVersionId; bool factsTruncated=false;
    qint64 sampleCount=-1, pseudoLabelCount=-1, missingTimeCount=-1;
    bool hasLabelSchema=false; QStringList labelClasses;
    QHash<QString,qint64> samplesByClass, samplesBySeason, samplesByYear;
    QStringList bandRoles; QString modality, sensor, crsWkt;
    SpatialExtent extent; bool hasTemporalExtent=false; QDateTime temporalStartUtc, temporalEndUtc;
};
class SuitabilityDataProvider {   // 规模查询唯一通道
    virtual Result<DatasetFacts> datasetFacts(const QString& versionId, const FactsLimits&) = 0;
};
struct FactsLimits { int maxClassValues=64; qint64 maxSampleScan=50000; }  // 超限 → factsTruncated + uncertainty source
```

### 4.5 评估入口

```cpp
struct SuitabilityAssessor::Inputs {
    SuitabilityGoal goal;
    QVector<SceneCandidate> scenes;              // 可空
    std::optional<DatasetFacts> facts;           // 可空；与 provider 二选一或都给
    const SuitabilityDataProvider* provider = nullptr;
};
static Result<SuitabilityReport> assess(const Inputs&);
// typed failure："suitability.empty_subject"（scenes 与 facts 均无）、
// "suitability.profile_unknown"、"suitability.goal_invalid"。
```

### 4.6 Criteria 目录（稳定 id）

| id | 判定核心 | Unsuitable 条件（典型） |
|---|---|---|
| `spatial.coverage` | AOI ∩ ⋃scene 矩形（同 CRS 才算，异 CRS→Unknown+note） | 覆盖率 < minCoverageFraction |
| `spatial.resolution` | scene gsd ∈ [minGsd,maxGsd] | 可用场景为空 |
| `temporal.coverage` | 获取时间 ∈ 窗口 | 窗口内为 0 |
| `temporal.density` | 窗口内场景数 ≥ profile 需求 | 低于最低密度 |
| `temporal.seasonality` | 场景月份→季节（北半球气象季，note 声明）∈ requiredSeasons | 缺必需季相 |
| `quality.cloud` | cloudCover ≤ max；未知云量→unknown 贡献 | 全部超限 |
| `spectral.bands` | requiredBandRoles ⊆ ⋃scene/facts bandRoles | 必需波段缺失（每缺一个一条 gap） |
| `labels.availability` | schema/类别/样本数/pseudo-label 政策 | 缺 schema、必需类缺失、样本 < min |
| `grid.compatibility` | compareGrids 抽样对（cap 200 对，超限 note）→ profile strictness | blocking 失配且 profile 严格 |
| `model.compatibility` | model 需求 vs 数据（波段/分辨率/模态）；goal 无 model→not applicable | 模型输入需求不满足 |
| `uncertainty.sources` | 汇总全部假设/截断/未知数（blocking 划分） | 存在 blocking 不确定源 |

## 5. Migration / compatibility

新模块零迁移。schema versioned（`kSuitabilityReportSerializationVersion=1`、goal JSON 同理）；未来字段演进按"版本升号 + fromJson 拒绝 foreign version（typed failure）"的仓库既有协议。中央文件 delta 仅 3 处：顶层 `CMakeLists.txt` add_subdirectory 一行、`tests/CMakeLists.txt` 测试 target、`.gitignore` planning allow-list 两行。

## 6. Observability

每条 criterion 携带 evidence（measured vs required）+ diagnostics + notes；report `contentDigest()` 支持下游 provenance 关联（未来 experiment 侧接线，见 integration.md）；provider 截断显式入 `factsTruncated` 并生成为 uncertainty source——沉默截断是 bug。

## 7. Security / trust boundary

纯计算、无网络、无文件 I/O、无路径解析；JSON 只接受本模块自身 schema（QJson，非 jsoncpp，与 #1154/#1155 的 O4 面无关）；provider 由调用方注入，assessor 不自行打开数据源（不重蹈 #1164 containment 类问题）；不隐式"修正"任何科学状态——所有降级路径（CRS 不同、元数据缺失、截断）都落在 Unknown/notes/gaps，不留静默 fallback。

## 8. Performance budget

| 路径 | 预算 | 手段 |
|---|---|---|
| 单次 assess（≤1000 scenes + capped facts） | < 50 ms | 纯内存；矩形 union 坐标压缩 O(n²) 格 ≤ 4e6 单元；无 I/O |
| facts 获取 | provider 侧 LIMIT 显式 cap（maxSampleScan=50000、maxClassValues=64） | 超限截断 + 上报 |
| grid 兼容 | ≤ 200 抽样对（n>20 时等步长抽样） | note 声明抽样 |
| 内存 | O(scenes + classes×seasons×years)，无全样本物化 | facts 是投影非行集 |

## 9. Test strategy

Catch2 平铺测试（`tests/test_suitability_*.cpp`），仅链 `Catch2 + Qt6::Core + sicnu_suitability(+sicnu_data/sicnu_dataset)`，不触 QGIS/GUI。每 slice 先 RED 后 GREEN。覆盖矩阵：happy path / invalid input（未知 profile、空 subject、foreign schema）/ boundary（阈值恰好、cap 恰好）/ serialization round-trip + 确定性重放（同输入两次 digest 相同、JSON 键序稳定）/ compatibility（fromJson v1）/ cancellation n/a（纯函数无长任务）/ teaching 与 agent 语义一致（同一 report 的叙事结论与 JSON level 一致）。

## 10. Work packages

Slice A schema/lattice → B spatial → C spectral+quality → D temporal → E labels/grid/model+provider → F profiles → G adversarial → H teaching+agent tool+integration docs+最终 review。详见 slices.md。

## 11. Rollback / kill-switch

纯新增模块，无既有调用点。回滚 = 删除 `src/suitability/` + 3 处中央 delta revert。无 feature flag 需求。

## 12. Definition of Done（track-specific 部分见 slices.md 尾部）

公共 DoD 全项 + 本 track 7 条（教学场景端到端、agent machine-readable、单一事实源、typed 失败、离线、预算上界、动态去重）。
