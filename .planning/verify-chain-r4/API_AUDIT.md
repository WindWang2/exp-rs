# API_AUDIT — 验证链 72 头文件逐行审计（Track 16）

基线 `15e5c66b54`。风险列：**L** = numeric-locale 敏感路径；**O** = 迭代序/注册序敏感；**B** = 字节来源/IO 敏感；**T** = 篡改/信封门。处置列：✅绿 = 本轨矩阵/既有门禁已钉死；📌 = 本轨登记的合同语义；📄 = 豁免（理由在 DECISIONS D5）。

## src/verify（11）

| # | 头 | 关键类型/签名 | 风险 | 处置 |
|---|---|---|---|---|
| 1 | projections.h | task/lab/teaching 投影（Verdict/LabVerdict 词表透镜） | O | ✅绿 — "One report, three projections, zero verdict flips" 既有案 + 81-格聚合格 |
| 2 | verify_context.h | IArtifactProbe/IGridProbe/IStateView/IProvenanceView/IMetricView + VerificationContext（5 裸 seam） | B | ✅绿 — 空槽=Declared gap（verify:i_provider_missing），engine 案 + 本轨五-kind 装配格 |
| 3 | verify_engine.h | hasEvaluator/evaluateCheck/evaluate | O,T | ✅绿 — 词表-引擎互锁 mutation oracle + WP-C 边界三案（空 spec/矛盾/kind 精确匹配） |
| 4 | verify_error_codes.h | verify:e_*/i_* 词表 + isVerifierCode/isIndeterminateCode | — | ✅绿 — 词表闭集，reader 拒绝外语义 |
| 5 | verify_levels.h | verifyPlanNodePostcondition/TaskOutcome::digest（L1/L2 折叠） | O,T | ✅绿 — no-swap 规则既有案；digest 经 canonicalJsonText（钉） |
| 6 | verify_locale.h | ClassicNumericLocale（per-thread uselocale/_configthreadlocale） | L | ✅绿 — 本轨双 locale 矩阵 V1-V5 行实证钉守有效 |
| 7 | verify_pack.h | VerifierPack/validatePack/parsePack/composePacks/packDigest | O,T | ✅绿 — specId 去重拒合并、预算、digest（钉）；locale 矩阵 V3 行 |
| 8 | verify_render.h | renderText/renderTeaching/nextActionFor（纯整数格式化） | L(无) | ✅绿 — ostringstream 仅格式化整数计数，无小数点路径 |
| 9 | verify_sha256.h | sha256Hex(string)/sha256Hex(uint8_t*,size)（FIPS 180-4 自包含） | B | ✅绿 — RFC 6234/NIST KAT 既有；本轨矩阵逐行复用 |
| 10 | verify_status.h | VerificationStatus 三态 + aggregateStatus（empty→Indeterminate） | O | ✅绿 — 81-permutation lattice fuzz 钉死折叠格 |
| 11 | verify_types.h | VerificationSpec/CheckResult/Report + specDigest/canonicalJsonText/parseSpec | L,O,T | ✅绿 — canonicalJsonText 入口单点钉 locale（D1-b）；拒非有限数哨兵；本轨 V1/V2/V5 行 |

## src/verify_adapters（5）

| # | 头 | 关键类型/签名 | 风险 | 处置 |
|---|---|---|---|---|
| 12 | bounded_io.h | readFileBounded/parseJsonBounded/pathFromUtf8/pathExists + kMaxAdapterDocumentBytes | B | ✅绿 — 本轨直测负例格（OverCap/Missing/坏 JSON）；UTF-8 路径契约（sdk path_policy 先例） |
| 13 | checkpoint_state_view.h | readCheckpoint（Ok/Missing/Unreadable/Oversized/ForeignEnvelope）+ CheckpointStateView 词表投影 | B,T | ✅绿 — 本轨补 Unreadable typed 格（此前枚举值零覆盖）；closed version set |
| 14 | fs_artifact_probe.h | FsArtifactProbe（存在/大小/shallow kind/digest 预算 64MiB） | B | ✅绿 — 预算超限→空 digest→引擎 Indeterminate 既有案 |
| 15 | gdal_grid_probe.h | GdalGridProbe（3×3 窗口原生采样，kDefaultSamplingAxis=32） | B | ✅绿 — 真实 GTiff/VRT/死 VRT 格（grid 套件）；CRS-less→"" 由引擎判 Mismatch |
| 16 | provenance_sidecar_view.h | kProvSidecarSchema/kRunProvenanceKind + readProvenanceSidecar/readRunProvenance（Ok/Missing/Unreadable/ForeignEnvelope） | B,T | ✅绿 — 三态 typed 全断言既有 + 外来信封永不喂完成态 |

## src/grader（8）

| # | 头 | 关键类型/签名 | 风险 | 处置 |
|---|---|---|---|---|
| 17 | grader_adapters.h | EvidenceProjector：provenance/checkpoint/metric-record → GradeEvidence | B,T | ✅绿 — 外来信封/结构破损 typed 拒绝 9 案既有 |
| 18 | grader_engine.h | grade(rubric, evidence) → GradeOutcome（fail-closed，无部分评分） | O,T | ✅绿 — WP-E 三案 + 预算超限 typed 拒绝既有 |
| 19 | grader_error.h | GraderError/grader:e-* 词表 | — | ✅绿 — 闭集；拒绝路径复用既有分类（WP-E 无新增语义） |
| 20 | grader_json.h | canonicalizeJson（std::to_chars，规范 locale 无关）/canonicalNumber | L | ✅绿 — 头内显式论证 + 本轨矩阵 G1 行实证（de_DE 下 digest 字节一致） |
| 21 | grader_matcher.h | 关键词界/概念匹配/规格化文本（locale-free 设计，LC_CTYPE 注记） | L,O | ✅绿 — 判定置换不变既有案 + locale-free 论证在头 |
| 22 | grader_render.h | 报告人读渲染 | L(无) | ✅绿 — 渲染不进 digest（digest= canonical body） |
| 23 | grader_sha256.h | Sha256 流式 + sha256Hex（Qt-free 自包含，第三份同款 KAT） | B | ✅绿 — 与 verify 同款 KAT；叶子隔离先例（D5） |
| 24 | grader_types.h | Rubric/Evidence/Report + verifyDigest/evidenceDigest（字节序绑定为篡改证据） | O,T | ✅绿 — permutation invariance + tamper 既有案；本轨 G1 行 + WP-E 三案 |

## src/preflight（12）

| # | 头 | 关键类型/签名 | 风险 | 处置 |
|---|---|---|---|---|
| 25 | asset_state_adapter.h | projectAssetState + StateAssetFactsProvider（resolver 形状与 broker 共享） | B | ✅绿 — 真护照投影全字段既有案 + 本轨 WP-D 共享权威 20 案 |
| 26 | capability_mirror.h | CapabilityMirrorProjection（extends 合并 child-wins/variant/深度界/损坏 fail-closed） | B,O | ✅绿 — 真镜像文档钉死既有案；病文档→Unavailable 永不静默 |
| 27 | engine.h | PreflightEngine（重复 id 拒/sorted-id 求值/requestDigest/rulesRevision/单点 ack/响亮截断） | O,T | ✅绿 — 17 既有引擎案 + 本轨 WP-D digest/预算/确定性 5 案 |
| 28 | facts.h | SlotFacts（缺省=typed unknown；temporal truncation 计数） | B | ✅绿 — "missing fact is a typed unknown" 由规则族案钉死 |
| 29 | finding.h | PreflightFinding（code/severity/evidence/basis）+ totalFindingOrder 输入 | O | ✅绿 — 同键 canonical 排序既有案 + 本轨 locale P4 行 |
| 30 | provider.h | IAssetFactsProvider/ICapabilityProvider + Memory* fakes | B | ✅绿 — 未注册 ref=Unknown 不虚构既有案 |
| 31 | render.h | 报告人读渲染 | L(无) | ✅绿 — 不进 digest |
| 32 | report.h | PreflightReport + toJson/fromJson（verdict 一致性门）+ reportDigest | L,O,T | ✅绿 — 本轨 locale P3/P5 行；fromJson 拒自相矛盾报告（WP-B 探针亲证）；reportDigest 经 jsoncpp writer（1.9.8 实证 locale 无关，D1-a） |
| 33 | rule.h | IPreflightRule/RuleResult（outcome 词表 pass|finding|insufficient_facts|skipped） | O | ✅绿 — 词表闭集 |
| 34 | rules.h | builtinRules 10 族 + rule_id 词表（preflight.*） | O | ✅绿 — 17 规则案；本轨 WP-D 断言全部用 rule_id 常量拼写 |
| 35 | runtime_adapter.h | 评测外层 runtime 投影 | B | ✅绿 — 独立套件既有 |
| 36 | sha256.h | sha256Hex/shortDigest（第三份自包含 SHA-256） | B | 📄豁免 — Qt-free 叶子不跨引用（与 grader 同款取舍）；KAT 同源；见 D5 |

## src/suitability（21）

| # | 头 | 关键类型/签名 | 风险 | 处置 |
|---|---|---|---|---|
| 37 | criteria_grid.h | 网格分辨率判据 | B | ✅绿 — adversarial 套件 25 案覆盖族 |
| 38 | criteria_labels.h | 标签可用性判据 | B | ✅绿 — 截断降级可见（truncation_downgraded）案 |
| 39 | criteria_model.h | 模型契合判据（band_missing gap 族） | B | ✅绿 — model-pinned 缺证据→Unknown 案 |
| 40 | criteria_spatial.h | 覆盖/AOI 判据（1e300 极值拒绝） | B | ✅绿 — aoi_area_not_representable/coverage.no_usable_scene 案 |
| 41 | criteria_spectral.h | 波段判据（QString::arg(double) 仅人读 summary；Qt6.11 实证 locale 无关） | L(无) | ✅绿 — 本机 Qt 探针 + 本轨 S1 contentDigest 行 |
| 42 | criteria_temporal.h | 时序判据（窗口/乱序/截断） | B,O | ✅绿 — temporal 案族 + 采样尾界案 |
| 43 | criteria_uncertainty.h | 不确定性汇总 | O | ✅绿 — Unknown 压制 Suitable 排序案 |
| 44 | dataset_facts.h | DatasetFacts（-1=unknown 约定；factsTruncated） | B | ✅绿 — 0 与 -1 是不同测量值（store 案）；截断必标记 |
| 45 | scene_candidate.h | SceneCandidate | B | ✅绿 — NaN/inf/负 GSD typed unknown 案 |
| 46 | seasonality.h | 季节性工具 | O | ✅绿 — criteria_temporal 案族 |
| 47 | store_data_provider.h | StoreDataProvider（null store→provider_unavailable） | B | ✅绿 — 6 案既有 + 本轨 WP-F 三案补 assess 组合格 |
| 48 | suitability_agent_adapter.h | agent 工具投影 | O | ✅绿 — agent_tools 套件既有 |
| 49 | suitability_assessor.h | assess(Inputs)→Result（typed failures；kMaxScenes=1000；provider 失效 typed 向上） | O,T | ✅绿 — 25 adversarial 案 + 本轨 WP-F：失败传播/优先级/咨询门控 |
| 50 | suitability_goal.h | SuitabilityGoal（敌意数值 goal_invalid 双路径） | B | ✅绿 — 1e300/±inf/NaN 内存+JSON 双路径案 |
| 51 | suitability_level.h | SuitabilityLevel/lattice（Unknown 压制） | O | ✅绿 — core 套件 |
| 52 | suitability_profiles.h | 8 builtin profiles 全字段钉表 | — | ✅绿 — profiles 套件 |
| 53 | suitability_provider.h | SuitabilityDataProvider/InMemoryDataProvider（注入式失败）/FactsLimits | B | ✅绿 — WP-F 直接消费本接口的失败注入构造 |
| 54 | suitability_report.h | SuitabilityReport（criteria 规范序/gap 去重/contentDigest） | O | ✅绿 — core 套件 + 本轨 S1 locale 行 |
| 55 | suitability_teaching.h | 教学投影 | O | ✅绿 — teaching 套件 |
| 56 | suitability_time.h | 时间工具 | B | ✅绿 — temporal 案族 |
| 57 | suitability_types.h | Criterion/Gap（id 规范序、重复精确折叠） | O | ✅绿 — core 案 |

## src/science_context（15）

| # | 头 | 关键类型/签名 | 风险 | 处置 |
|---|---|---|---|---|
| 58 | agent_adapter.h | agent 工具端点 | O | ✅绿 — 四端点 parity 案 |
| 59 | asset_state_provider.h | AssetStateProvider（resolver/typed 失效通道/invalidate/clearCache） | B,T | ✅绿 — WP-D 共享 resolver 20 案；gdal_open_failed≠asset_not_found 区分案 |
| 60 | broker.h | ScienceContextBroker（4 失效接缝 + synthesize + 永非第二存储） | O,T | ✅绿 — 33 broker 案 + 14 live 案 + 本轨 WP-D |
| 61 | bundle.h | ScientificContextBundle + serializeBundle/computeBundleId（同输入 byte-stable） | O,T | ✅绿 — byte-stable 既有案 + 本轨 SC1 locale 行（含小数 passport 几何） |
| 62 | capability_facts.h | CapabilityFactsLookup（entriesForIntent 必须全评估/revision 每次 synthesize 读） | O | ✅绿 — 多候选/variant/authority 接管案 + WP-D revision 行 |
| 63 | capability_router.h | CapabilityRouter（unknown intent→impossible） | O | ✅绿 — live authorities 案 |
| 64 | context_budget.h | applyContextBudget（确定性截断元数据） | O | ✅绿 — 预算裁剪/超长 intent 案 |
| 65 | context_cache.h | ContextCache（LRU 128 确定性驱逐/命中计数） | O | ✅绿 — LRU 有界+受害者确定性案；WP-D 失效行（null-registry 语义 📌 D2 精化） |
| 66 | gdal_asset_source.h | GDAL 资产源 | B | ✅绿 — gdal_open_failed typed 案 |
| 67 | live_asset_resolver.h | LiveAssetResolver | B | ✅绿 — live authorities 真 GDAL 案 |
| 68 | observability.h | BrokerObservability（计数离 bundle 字节） | O | ✅绿 — byte-stable 契约案 |
| 69 | observed_state.h | observedStateFromPassport | B | ✅绿 — 经 SC1 行（序列化字节含其输出） |
| 70 | planner_goal_projection.h | projectPlanningContext（goalId 绑定 bundleId+intent+资产集） | O | ✅绿 — planner DoD 案 + goal_projection 套件 |
| 71 | planner_projection.h | 执行闸门投影（executionBlocked） | O | ✅绿 — conflicted 全链不降级案 |
| 72 | recipe_router.h | RecipeRouter（registry 同步/sourceInfo 三态） | O,B | ✅绿 — live pack/reload fail-closed 案 + WP-D refreshRecipes 行 |

## 审计结论

- 72/72 行处置非空；零"未声明缺口"。
- 生产代码**零修改**：六模块经 #1285/#1318/#1277/#1279/#1320/#1292/#1324 六轮收口后，本轨实测（72 头审计 + 双 locale 矩阵 + 失效联动矩阵 + 12 个边界增量案）未发现可判定的生产缺陷。本轨交付为**合同钉死**（可执行验证）而非修码 —— 与 Scope 铁律"只优化修复、拒绝为做而做"一致。
- 非本域发现（登记不处置）：`cmake/raise-compiler-stack.sh` 退出码捕获 bug（假绿陷阱，build-infra 轨所有）→ PR 正文与 EVIDENCE.md 披露。
- metric seam（IMetricView 无生产 adapter）：ADAPTER_MATRIX.md 声明"不处置"——词表消费端（metric.range/relational.consistency）由 fakes 全覆盖，生产侧无对应事实源属产品空位非缺陷。
