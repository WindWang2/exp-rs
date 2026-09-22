# Progress — RS14-16-scene-suitability

| Slice | 状态 | 证据 |
|---|---|---|
| A schema/lattice | ✅ 完成 | commit `7f957a4dba`；`test_suitability_core` 11 cases / 68 assertions 绿 |
| B spatial | ✅ 完成 | commit `6317945048`；`test_suitability_spatial` 18 cases / 201 assertions 绿 |
| C spectral/quality | ✅ 完成 | commit `269c69caab`；`test_suitability_spectral` 10 cases / 97 assertions 绿 |
| D temporal | ✅ 完成 | commit `1fb59a93aa`；`test_suitability_temporal` 13 cases / 79 assertions 绿 |
| E labels/grid/model + provider | ✅ 完成 | commit `044b3780c9`；`test_suitability_labels` 24 cases / 115 assertions 绿，`test_suitability_store_provider` 5 cases / 226 assertions 绿（真实 temp-file DatasetStore） |
| F profiles | ✅ 完成 | commit `660b1934ba`；`test_suitability_profiles` 10 cases / 91 assertions 绿 |
| G adversarial | ✅ 完成 | 本条更新时写入；`test_suitability_adversarial` 21 cases / 347 assertions 绿（RED 先行：对未修复源码 10 断言失败，修复后全绿）；全部 8 个 suitability 套件 112 cases / 1214 assertions 绿 |
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
- 2026-09-21 (Slice G): **发现并修复 6 类"假绿"缺陷**（修复前 `test_suitability_adversarial` 10 断言 RED，修复后全绿；全部既有测试语义未动）：
  1. **GSD 脏数值洗白（P0）**：`spatial.resolution`/`model.compatibility` 把 NaN/inf/负数 GSD 当有效证据——NaN 与任何数比较皆 false，`inRange(NaN)==true`，全 NaN 场景集判 Suitable，且 evidence 出现 ±inf。修复：非有限或非正 GSD 按 unknown 计（`gsd_invalid_count` + Warning `suitability.gsd_invalid`），model 侧同理过滤，且 model 钉了 GSD 区间却无任何有效 GSD 证据 → Unknown（`gsd_evidence_absent`/`gsd_evidence_invalid`），绝不 Suitable。
  2. **NaN 云量误判（P0）**：`quality.cloud` 的 `cloud < 0 || cloud > 100` 对 NaN 双 false → 计入 known 且非 clear → 全 NaN 判 Unsuitable（垃圾输入的假阴性）。修复：改用 `!(cloud >= 0 && cloud <= 100)`，NaN 归入 out-of-range unknown + 既有 `suitability.cloud_out_of_range` Warning。
  3. **极端 AOI 覆盖率浮点爆炸（P0）**：AOI 边长 1e-300（面积下溢为 0，0/0=NaN）判 Unsuitable 且 evidence 携带 NaN；边长 1e300（面积上溢 inf）可能 inf/inf=NaN。修复：measured_fraction 非有限 → Unknown + `suitability.aoi_area_not_representable`（拒绝在该尺度下测量的诚实答案，不做隐式重缩放）。场景 extent 四角含 NaN 时同样下毒坐标排序（std::sort 比较子失效）→ 按 unknown extent 折叠计数。
  4. **goal 整数字段 UB（P0）**：fromJson 对 min_samples/min_scenes_in_window 直接 `static_cast<qint64>(toDouble())`，JSON `1e300`（或 1e999 解析出的 inf）触发 C++ 未定义行为。修复：`boundedIntegral` 门（有限 + |v| ≤ 2^53 精确整数区）→ typed `suitability.goal_invalid`。
  5. **goal 荒谬数值静默通过（P1）**：NaN minGsd/cloud 上限/coverage 因比较全 false 被当"未设置"；负 maxGsdM（如 1e300/-1e300 组合）静默退化为"无上限"。修复：validateGoal 对全部 double 字段加有限性检查 + GSD 下限非负 → `suitability.goal_invalid`。
  6. **截断沉默（P0，语义决定见下）**：`factsTruncated` 只在 labels evidence 里躺着一个布尔，无任何判定消费它（E 决策"截断→uncertainty source"未落地）；store provider 的 label_source/pseudo_label/missing_time facet 在 "(other)" 折叠桶存在但目标值可见时**不置截断位**（部分计数冒充总数）。修复：折叠桶存在 → 一律 `factsTruncated=true`；labels.availability 可用且本应 Suitable → 降为 Marginal（`truncation_downgraded` 证据 + note）。
- 2026-09-21 (Slice G): **overall "全 not-applicable" 语义（锁死）**："什么都没要求"≠"适合"。assess() 层不可能全 NA——`spatial.coverage`（无 AOI→Unknown）与 `temporal.coverage`（无窗口→Unknown）无条件 applicable，纯探索 subject 的 overall 恒为 Unknown；report 层全 NA 聚合 = Unknown（empty aggregate 首个证据都不存在时拒绝主张）。可见的例外：被完全测量的 criterion（如 3 网格全比对通过）保持 Suitable 不违反语义——overall 由 Unknown 压住。测试锁定。
- 2026-09-21 (Slice G): **截断语义（锁死）**：①labels.applicable + factsTruncated → Suitable 降 Marginal（任何带标签需求的报告在截断下不可能 overall Suitable）；②labels 不可适用时截断仍以 note + `facts_truncated` 证据出现在报告 JSON（其余 facts 消费者读的是 manifest 派生字段或只往保守方向失败，故 overall 可保持，但截断必须可见）；③Unsuitable/Unknown 不因截断改判（已 fail-closed）。
- 2026-09-21 (Slice G): **重复 gap 语义（锁死）**：`allGaps()` 按"id+criterion+description+evidence 完全相等"去重（补齐 slices.md Slice A 的"排序去重"规格；A–F 只实现了排序）——重复输入行不得读成两条发现；同 id 不同证据仍是各自发现。labels 的 requiredClasses 重复/空码在 gap 生成前去重/跳过（evidence 保留原始列表）。
- 2026-09-21 (Slice G): **profiles 位置初始化加固**：`makeBuiltinProfiles` 的 12 参位置聚合初始化改为具名字段构造（baseProfile + 逐字段赋值）——新增字段变为编译安全的编辑；新增全表逐 key 默认值 pin 测试防科学先验漂移。
- 2026-09-21 (Slice G): 其余对抗性结论（无需修复，测试锁死）：grid 等步长抽样 linear→(i,j) 译码与暴力全比对拍一致（n=12 全集 66 对 + n=21 抽样 200/210 期望阻断数 + n=203 抽样确定性）；重复/空 scene id 不参与判定只透传报告（镜像输入，确定性锁定）；畸形 JSON（非对象 criteria、垃圾/200 层嵌套 evidence、100k 字符 summary、重复 criterion id=last-wins）要么 typed 失败要么字节稳定 round-trip；重放确定性（QHash 反序插入双跑逐字节相同 + digest 相同）复证 Slice C 排序决策。
- 2026-09-21 (Slice G): observed 未修：①`temporal.coverage` 存在窗口内 1 景 + 其余时间未知即 Suitable（存在性判定，方向保守，B/D 已锁）；②`spatial.resolution` 部分场景 gsd 未知仍可 Suitable（B 决策"未知不参与判定"锁定，依赖 gsd_invalid 诊断保持非沉默）；③抽样模式下"抽样内 0 阻断 → Suitable"有 note 声明（E 决策锁定）；④季节映射北半球假设不变（D 决策）。**master open issues 触碰检查**：本模块不触 #1151（capability mirror——无任何 capability 镜像读写）、不触 #1184（10k 截断——模块自身 cap 体系独立：1000 scenes/200 pairs/50000 rows/64 facet values），仅报告不修。

## Slice H 记录（主智能体收尾，2026-09-22）
- H1/H2 由被中断的前任 agent 提交（8d5e531e16、b9eb449c17），质量良好。
- H3 重构：适配器从 src/agent 下沉到 `src/suitability/suitability_agent_adapter.{h,cpp}`（sicnu_suitability 内），agent 侧只留薄壳（defs 两行 + prefix 一行 + dispatch 两分支 + CMake 链接一行）——动机：qgis_agent 全量链接需数小时，adapter 在 Qt-only 层可直接单测（5 cases / 26 assertions 绿）。dataset_version_id 无 dataset_db → 拒绝（拒绝无证据源的点名查询）。
- capability knowledge / kDataPlatformPrefixes 刻意不动（#1151/#1187 避让）；surface parity 由既有 test_surface_parity 自动覆盖（sicnu_agent 构建在后台进行，PR 前验证）。
- H4：integration.md（track-local 接线点文档）落地。
- 最终套件（11 个，全部绿）：core 68/11、spatial 195/18、spectral 93/10、temporal 79/13、labels 115/24、store_provider 226/5、profiles 92/10、adversarial 350/21、uncertainty 50/7、teaching 76/5、agent_tools 26/5（assertions/cases）。
- observed：sicnu_agent + test_data_platform_surface/test_surface_parity 全量构建耗时长（qgis_core ~1000+ TU，-j1），后台进行中，PR 前回收结果；test_capability_drift 在 master 本就红（#1151），本 track 不新增红灯 case。

## Review Gate 记录（2026-09-22）
- Round 1（独立 reviewer）：无 P0；P1-1 DatasetFacts::fromJson 整数 UB（已修+变异 RED 锁定，commit 3580d4b7a8）；P1-2 agent surface 构建验证（进行中）；P2×4（两条文档注释已补，两条裁定 follow-up）；变异杀测试 5/5 有效；1000 场景实测 7.7ms（预算 50ms）；4 线程并发干净；8 个兄弟 RS14 track 零边界踩踏。
- Round 2（re-review）：P1-1 修复双重验证通过，11 套件全绿（1385 assertions/111 cases），验证一致性/确定性/teaching 退化路径全 pass；新发现 3 个 P3（hashFromJson 诊断不带具体键、JSON 错误类型静默降级无专测、unicode round-trip 无专测）→ follow-up 不阻塞。**Verdict：可提 PR。**
- follow-up 清单（非阻塞）：① assetState CamelCase 词汇第二副本下沉 sicnu::data；② goal fromJson 错误类型标量 typed 失败；③ 测量冲突档位第三档（可剔除证据，Marginal+明细）；④ P3-a/b/c。

## P1-2 回收（2026-09-22，sicnu_agent 全量构建完成后）
- `libsicnu_agent.so` + `test_data_platform_surface` + `test_surface_parity` 构建成功（含新 Sicnu::suitability 链接与薄壳接线）。
- `test_data_platform_surface`：**5 cases / 105 assertions 全绿**（直调 handleDataPlatformTool 的 surface 契约）。
- `test_surface_parity`：7/8 cases 绿；唯一失败是 CLI 子进程 leg（`:340 waitForStarted`）——`sicnu_geo_rs_cli` 二进制在本 worktree 的部分构建树中不存在（`build-dev/src/cli/` 无产物），属环境性缺失，与本改动无关；与本改动直接相关的 dispatch-probe leg（每个 data-platform def 必须有 handler、绝不 "unknown data-platform tool"）与投影 parity leg 全绿。CI 全量构建下该 leg 将正常执行。
- capability drift 面：按构造未触碰（kDataPlatformPrefixes/knowledge 零改动）；#1151 既有红灯与本 track 无关。

## 收尾（2026-09-22）
- Union merge master `14bef28949`（46 commits）：`.gitignore` 白名单并集、tests/CMakeLists.txt 追加块并集；agent 薄壳自动合并核验完好。
- 合并后回归：11 个 suitability 套件全绿；`test_data_platform_surface` 5 cases / 105 assertions 绿；`test_surface_parity` 3962/3963（唯一失败为 `sicnu_geo_rs_cli` 二进制未构建的环境项，与本改动无关，CI 全量构建下正常）。
- **PR #1236 已创建**：https://github.com/WindWang2/exp-rs/pull/1236（未等 CI，不以 CI 为完成条件）。
- Track Definition of Done 对照：①本科生端到端场景=profiles 套件 classification/phenology mini-scenario ✅ ②agent machine-readable=agent_adapter+工具+digest 复核 ✅ ③单一事实源=只调 compareGrids/复用 BenchmarkTaskFamily/Result 词汇（review 双轮确认）✅ ④typed 失败全路径（round1 round2 验证）✅ ⑤离线可用（全部套件无网络）✅ ⑥资源上界（caps+实测 7.7ms/预算 50ms）✅ ⑦动态去重（10 个兄弟 PR 零重叠+union merge 记录）✅
- follow-up 清单见 Review Gate 记录节。
