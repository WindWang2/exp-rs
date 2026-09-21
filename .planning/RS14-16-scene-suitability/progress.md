# Progress — RS14-16-scene-suitability

| Slice | 状态 | 证据 |
|---|---|---|
| A schema/lattice | ✅ 完成 | commit `7f957a4dba`；`test_suitability_core` 11 cases / 68 assertions 绿 |
| B spatial | ✅ 完成 | commit `6317945048`；`test_suitability_spatial` 18 cases / 201 assertions 绿 |
| C spectral/quality | ✅ 完成 | commit `269c69caab`；`test_suitability_spectral` 10 cases / 97 assertions 绿 |
| D temporal | ✅ 完成 | commit `1fb59a93aa`；`test_suitability_temporal` 13 cases / 79 assertions 绿 |
| E labels/grid/model + provider | ✅ 完成 | commit `044b3780c9`；`test_suitability_labels` 24 cases / 115 assertions 绿，`test_suitability_store_provider` 5 cases / 226 assertions 绿（真实 temp-file DatasetStore） |
| F profiles | ✅ 完成 | commit（本条更新时写入）；`test_suitability_profiles` 10 cases / 91 assertions 绿 |
| G adversarial | 未开始 | — |
| H teaching/agent/docs | 未开始 | — |

## 决策记录
- 2026-09-21: 基线 `4f6632e1f6`，无 open PR；worktree `exp-rs-wt-rs14-suitability`。
- 2026-09-21: 不复用 AuditVerdict（QA 语义≠目标匹配语义）；新增 SuitabilityLevel，秩序 Unknown > Suitable（fail-closed，对齐 QA "never Pass on partial evidence"）。
- 2026-09-21: integration 接线点写 track-local `.planning/<track>/integration.md`，不写中央 docs/integration.md（避免与 19 并行 track 撞同一文件）。
- 2026-09-21: ctest 测试名无 TEST_PREFIX 时为 Catch2 用例名；narrow 验证用直接执行测试二进制。
- 2026-09-21 (Slice B): SceneCandidate.gsdM 只接受显式 "gsd_m" hint；structure 的 CRS 单位像素尺寸仅作 `pixelSizeCrsUnits` 证据，永不冒充米（测试双向锁定）。acquisition hint 选定 ISO 字符串（Qt::ISODate），无时区按 UTC 读，解析失败字段留空（unknown 不伪造）。
- 2026-09-21 (Slice B): goal 校验单闸门 `validateGoal` 同时服务 fromJson 与 resolveRequirements；profileKey 非空在本 slice 一律 `suitability.profile_unknown`（Slice F 落 profile 表）。fromJson 对值域违规同样 typed failure（垃圾 JSON 不产出"看起来正常"的 goal）。
- 2026-09-21 (Slice B): spatial.coverage 判定顺序 = AOI 缺失/无效→Unknown → AOI 退化（面积≤0）→Unknown+diagnostic（目标本身畸形优先于数据缺失判断）→ 无 usable+georef 场景→Unsuitable（coverage.no_usable_scene）→ CRS 过滤（空 WKT 视为不等，全部排除→Unknown+note，不重投影）→ 坐标压缩矩形并（y-strip + x-sweep，O(n²logn)，1000 景安全）→ ≥阈值 Suitable；>0 且 ≥0.5*阈值 Marginal，否则 Unsuitable（gap coverage.below_minimum，evidence measured/required_fraction；0.5 分级有测试锁定）。
- 2026-09-21 (Slice B): assess 分辨率上限 kMaxScenes=1000 超限 → typed `suitability.too_many_scenes`（诚实拒绝而非静默截断）；空 subject（无 scenes、无 datasetVersionId、无 AOI）→ `suitability.empty_subject`。
- 2026-09-21 (Slice B): resolution 的 gsd 未知场景计入 evidence `gsd_unknown_count` 不参与判定；已知集合为空 → Unknown；超限场景 id 证据上限 20，超出记 `omitted_count`。边界含端点（gsd==min/max、coverage==阈值 均按满足/在限内）。
- 2026-09-21 (Slice C): DatasetFacts 的 QHash 成员序列化前按键排序（QHash 遍历无序），同内容两份 toJson 逐字节相等，回归测试用不同插入顺序锁定。
- 2026-09-21 (Slice C): spectral.bands 的 role 匹配与 gap id 归一化到 band_role.h 词汇形态（"NIR"→nir、"Red Edge"→red_edge，trim+lower+空格转下划线）；必需 role 缺 ANY 一个 → Unsuitable（每缺一个一条 gap `band.missing.<role>`，不降级 Marginal）；scene 池只收 usable 场景；band 元数据全缺 → Unknown+note "no band role metadata"。
- 2026-09-21 (Slice C): assessCloudCover 云量在 [0,100] 之外按 unknown 处理并记 Warning diagnostic（`suitability.cloud_out_of_range`），绝不 clamp；全部已知 ≤max → Suitable；部分清爽 → Marginal；全部超限 → Unsuitable（gap `cloud.cover_exceeded`，evidence 含 clear/known/unknown/worst）。maxCloudCoverPercent<0 → not_applicable。
- 2026-09-21 (Slice C): assess() Inputs 增加可选 DatasetFacts；带 datasetVersionId 的 facts 算合法 subject（facts-only 评估合法），身份为空的 facts 不算；report criteria 增至 4 个规范序 id。
- 2026-09-21 (Slice D): 季相映射取北半球气象季（冬 12/1/2、春 3-5、夏 6-8、秋 9-11），假设在 seasonality.h 头注释声明——goal schema 无目标半球字段，南半球需求须先加显式偏移参数再复用。
- 2026-09-21 (Slice D): temporal.coverage 时间源 = usable 场景 acquisitionTimeUtc（窗口含端点）∪ facts.hasTemporalExtent 范围相交；facts 范围与窗口完全不相交视为"已测得无数据"→Unsuitable 而非 Unknown。
- 2026-09-21 (Slice D): temporal.density 的 facts 范围相交只贡献 1 个"时间簇"估计（extent≠场景数），note 声明估计方式、evidence 拆分 scene_in_window_count 与 facts_temporal_contributed；恰好压线 → Marginal + gap `temporal.below_min_density`（evidence at_minimum=true），低于 → Unsuitable（同 gap id）。
- 2026-09-21 (Slice D): temporal.seasonality 池 = 场景月份派生季相 ∪ facts.samplesBySeason 值>0 的键（零值不算证据）；每个缺失必需季相一条 gap `season.missing.<season>`，ANY 缺失 → Unsuitable。
- 2026-09-21 (Slice D): assess() 报告增至 7 个规范序 criteria（quality.cloud < spatial.coverage < spatial.resolution < spectral.bands < temporal.coverage < temporal.density < temporal.seasonality）；Slice B/C 的 assessor 集成测试随报告形态更新（B 的 happy-path subject 补窗口+场景时间保持 overall Suitable——无 AOI 的 subject 会按 partial-evidence 规则把 overall 压在 Unknown，属预期语义）。
- 2026-09-21 (Slice E): 报告增至 10 个规范序 criteria（新增 grid.compatibility < labels.availability < model.compatibility 按字典序插入）；StoreDataProvider 投影决策——①hasLabelSchema 主源 = version manifest 的 labelSchemaRef；manifest 解析失败时诚实降级为"class facet 存在即有词汇在使用"（推断来源写代码注释），两者皆无才 false；②pseudoLabelCount 仅走 facet（label_source 值 "pseudo" 或 pseudo_label 真值列），SampleRecord 本身无伪标签字段、行扫描产生不了该证据，无 facet 记 -1（unknown 不伪造）；facet 尾被折叠且 pseudo 不可见时同样 -1 + truncated（折叠会藏住未知词表值，禁止假 0）；③missingTimeCount 走行级限额的 keyset 扫描（限额在行粒度检查——一页 500 行会一次带全全部样本，页级检查永远触发不了 cap），触顶 → count 保持 -1（部分扫描不许冒充总数）+ factsTruncated=true；④class/season/year facet 的 store "(other)" 折叠桶 = 截断信号，桶本身不入 facts map；⑤schema 派生字段（bandRoles/modality/sensor/crs/extent/temporal）全部来自 manifest，解析不了就留空，绝不编造。versionId 查不到 → `suitability.dataset_unknown`（缺失的主体 ≠ 空证据），store 读错误原样透传。
- 2026-09-21 (Slice E): labels.availability 判定序 = requireLabels=false → NA → 无 facts → Unknown（"no dataset facts provided"）→ hasLabelSchema=false 且 labelClasses 空 → Unsuitable（labels.schema_missing：需求已声明，证据全无即不满足）→ requiredClasses 逐类 gap `label.class_missing.<code>`（类码精确匹配）→ sampleCount 已知且 < minSamples → `samples.below_minimum` → sampleCount=-1 且 minSamples>0 → Unknown（unknown volume 永不冒充 0）→ pseudoLabelCount>0 且政策禁止 → Marginal + `labels.pseudo_present`；硬 gap（schema/类/量）压过 pseudo 降级。
- 2026-09-21 (Slice E): grid.compatibility 完全委托 compareGrids（单一事实源），本 criterion 只抽样计数：可用场景 <2 → NA（单景/无网格无组合可判）；可用 ≥2 但带 grid 的 <2 → Unknown + note "scenes lack grid snapshots"（有网格疑问不许装看不见）；>200 对（kMaxGridPairs）→ 等步长抽样 200 对 + note "sampled N of P pairs"（线性对索引等步长，确定性）；blocking 失配 → gridStrict ? Unsuitable : Marginal，gap `grid.blocking_mismatch.<首个 issue code>`；非 blocking（NoData 警告）不影响 Suitable；非有限 geotransform 项或 compareGrids 抛错 → Unknown + diagnostic。
- 2026-09-21 (Slice E): assess() 只在"无显式 facts 且点名了 datasetVersionId"时才咨询 provider；provider 失败原样上抛（点名了数据集就不许静默无 facts 继续）；显式 facts 优先于 provider；provider 非空但无版本号 → 不咨询（空 subject 规则照旧兜底）。
- 2026-09-21 (Slice F): 内置 profile 表 8 项（7 族同名默认 + phenology 附加）；空 profileKey → taskFamily 同名默认 profile；phenology 仅 TemporalPrediction 族可选（其他族选它 → profile_unknown，Slice B 的既有回归测试因此保持语义）；goal 显式值永远覆盖 profile 默认——bool 字段（requireLabels）的 false 是 schema 缺省态，无法表达"显式关闭 profile 的 true"（goal JSON v1 无三态，已在 profile 文档声明该限制）；pseudoLabelsAllowed/gridStrict 无 goal 字段，纯 profile 驱动（change_detection 独禁伪标签：benchmark 语义——评估不得拿模型派生标签打分）。
- 2026-09-21 (Slice F): profile 叠加后 Slice E 的 criterion 级 fixture 三处随动（labels 测试改用 TemporalPrediction 族/direct requirement 保持 profile 无关）——属 profile 默认生效的预期行为变化，非语义回退；B 的 deterministic-report 测试补齐 label facts（classification 默认 requireLabels=true 后，fully-answered subject 必须带标签证据才保持 overall Suitable）。
