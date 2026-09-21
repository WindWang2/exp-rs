# Progress — RS14-16-scene-suitability

| Slice | 状态 | 证据 |
|---|---|---|
| A schema/lattice | ✅ 完成 | commit `7f957a4dba`；`test_suitability_core` 11 cases / 68 assertions 绿 |
| B spatial | ✅ 完成 | commit `6317945048`；`test_suitability_spatial` 18 cases / 201 assertions 绿 |
| C spectral/quality | ✅ 完成 | commit `269c69caab`；`test_suitability_spectral` 10 cases / 97 assertions 绿 |
| D temporal | ✅ 完成 | commit `1fb59a93aa`；`test_suitability_temporal` 13 cases / 79 assertions 绿 |
| E labels/grid/model + provider | 未开始 | — |
| F profiles | 未开始 | — |
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
