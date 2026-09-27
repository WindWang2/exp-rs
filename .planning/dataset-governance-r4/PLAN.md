# PLAN — Track 13: Dataset Governance Hardening R4

顺序按依赖：WP-A（canonical 口径）→ WP-B（切分，依赖 A 的指纹稳定）→ WP-C/D/E（A/B 后并行推进，各自 Tracer Bullet 自洽）→ WP-F → WP-G 收口（最后固化矩阵）。

## WP 执行计划与实测修正

### WP-A 指纹确定性（30M）— 状态：完成（诚实 no-op：BOM 由 QString::fromUtf8 解码层吸收，矩阵固化两层平台行为）
- 新文件 `tests/test_dataset_fingerprint_determinism.cpp`：
  - KAV×1（独立 hashlib 真值 `ff5f7f56…a2cb`，手算 canonical 字节）+ 指纹字段豁免；
  - 双跑一致 + store 漏斗（textToJson/jsonToText）往返；
  - **扰动矩阵 12 组**（LF/CRLF、CRLF/CR、尾随换行 ×2、BOM、BOM+CRLF、缩进、pretty/compact、键序、整数拼写、零拼写、小数拼写）+ 逃逸拼写/canonical 幂等 + 负例内容行（串内路径分隔符=内容差异，指纹必须不同）+ 二进制/NUL loud-failure 行。
- 修复（红后）：**单点收敛在 `textToJson`**（dataset_store_impl.h:71，全部 store 读路径的解析漏斗）加 UTF-8 BOM 剥离；foundry_service.cpp:68 的独立 parse 改走同一漏斗（归一化单点，禁止散落各消费端）。
- 口径决策见 DECISIONS.md R1/R2。

### WP-B 切分可复现（26M）— 状态：完成（D1 修复 + KAV + 跨进程 tripwire）
- 新文件 `tests/test_split_reproducibility.cpp`：SplitMix64 公开参考向量（seed=0 与 0xDEADBEEF 两串）、Pcg32 固定算法向量、hashSeed/seedFor 向量 + 命名空间纪律、双跑 manifest digest、seed=0 合法且可复现、fold 逐条双跑、seed_hex 缺失即拒（红）、seed_hex=0 显式合法。
- 缺陷 D1（红已证预期）：`SplitConfig::fromJson`（split.cpp:511）`seed_hex` 缺失静默落 0，违反 split.h:69 契约"0 is a legal seed, absence is not"。修复：缺失→`dataset.split_invalid`（消息含 "seed"，随错误码族惯例）。兼容性核实：全部写方（split.cpp:479、run_bridge.cpp:81、reproduction_bundle.cpp:220）都写 seed_hex；test_contract_fuzz_ipc 的 total-function 断言不受影响。

### WP-C 泄漏审计覆盖（26M）— 状态：完成（C1a/C1b 修复 + 13 检查矩阵 + 排列不变性）
- **实测修正**：findings 顺序已有终排 `std::sort`（leakage_audit.cpp:620-627，键 (sampleA,sampleB,kind) 实践全序）——原候选 C1 不成立，转为**固化防回归**（输入排列不变性用例）+ 覆盖矩阵。
- 新增修复 A2（子代理审计发现）：`joinFeaturesBySampleId` 缺失必需列的 finding detail 按 QSet 序取"第一个"（feature_table.cpp:265-279）→ 报告内容跨跑不稳定。修为定序收集。
- 覆盖矩阵：13 个 kAllChecks × 构造性泄漏（同源跨角色/空间相邻/衍生特征/伪标签父链/事件对）逐项"报出集合==构造集合"；FoldComparabilitySummary honest-gaps 对齐枚举；fold 材料化审计；honest gaps 诚实标注。

### WP-D 本体校验 fail-fast（24M）— 状态：完成（载荷门 + 类码门 + R7 定位）
- 红点（实测缺口）：PointSample 坐标无 finite 检查（weight 有，口径不一致）；PixelSample 负列/行；PairSample 空/自配对引用；ObjectSample 空 objectRef；PolygonSample 非法 WKT（复用 wkt.h 解析器，不另起炉灶）；**注记未知类码静默入库**（addAnnotation 无 schema 对照，AnnotationRecord 自带 labelSchemaId/Version 可查 store）；错误串缺 `<record-id>`+字段定位。
- 修复原则：单一入口 `validateSample`/store 校验点前移；错误信息含 sample id + 字段名（先 grep 既有测试对消息文本的断言，防误伤）。
- 重复 id：store PK 防线已响（dataset_store_samples.cpp:214）——固化为防回归用例。

### WP-E 版本迁移（20M）— 状态：完成（A1 排序 + golden tripwire + 兼容矩阵）
- 实测现状：schema 版本仅 v1（kDatasetManifestSerializationVersion=1）；外版本报错已含实际+期望版本（dataset_manifest.cpp:186-192）。
- **修复 A1（子代理审计发现，红候选）**：`diffManifests` 的 added/removed/changed 向量按 QHash 序（dataset_version.cpp:81,89）→ diff JSON 跨跑字节不稳定。修为按 (kind,refId) 排序 + 双跑确定性用例。
- 交付：v1 golden fixture 入仓（内嵌 JSON + 独立 SHA-256 tripwire 常量，防 canonical 漂移的时间探针）；未知字段容忍/外版本拒绝/读向单程（无隐式写回）矩阵；DatasetVersionDiff 双跑确定性。
- 兼容矩阵诚实收缩：既有版本只有 1 个（"逐版本 golden" = v1 一行），虚报多版本才是作弊。
- **豁免记录 B1**：store 侧内容摘要（splits :30-34、samples :135-138）哈希 Compact 文本而非 RFC8785 canonical——与"唯一 canonical"口径分歧但已持久化，改口径会翻动既有存储 id；处置=文档化+误导参数名注释澄清，不改行为（DECISIONS.md R6）。

### WP-F data/ 目录一致性（20M）— 状态：完成（收窄域零源改动，探针全绿）
- **实测修正（Phase 0/1）**：`src/data` 内零个 dataset 头消费者（grep `dataset/dataset_*` 无命中）；dataset 版本/指纹的消费链在白名单外（src/cli、src/suitability、src/agent、src/experiment、src/app）。"src/data 侧 workspace 记录引用 dataset 指纹"前提不成立，按预案收窄不硬做。
- 收窄后的真实域：governance 镜像自身的引用一致性——`addRunOutputs` 批量路径是否像单条 `linkRunOutput`（#758-1）一样查两端存在性；`addLineageEdges` 是否接受两端不存在的边（悬挂 lineage）；`assetsByFingerprint` 大小写归一。红先证伪，悬挂引用负例构造。
- 冲突约束：governance_store.cpp 被 #1338 占据——若修复必须动它，改在未占用的调用侧（relink_service/workspace_service），并在 PR 声明 rebase 需求；探针测试落新文件 `tests/test_data_governance_consistency.cpp`（不碰 #1335/#1337/#1338 占用的三个 test_data* 文件）。

### WP-G 防回归收口（16M）— 状态：完成（注入自证 + Oracle 双轮 + 对抗评审 11 项全处置）
- 全量 `ctest -R "dataset|fingerprint|split|leak|ontology" -j1` 双跑；矩阵注入自证（临时改一字符 → 红 → 还原）；DECISIONS.md ≥5 条逐条对代码；22 头 API_AUDIT.md 处置列非空；EVIDENCE.md 双跑日志 + 矩阵逐组结果。

## 3.2 下限对照追踪

| 下限 | 目标 | 当前 | 载体 |
|---|---|---|---|
| 扰动矩阵 | ≥12 组 | 12 组 + 3 补充行（待绿） | test_dataset_fingerprint_determinism.cpp |
| 确定性用例 | ≥18 | 规划 24（A:6 B:7 C:5 D:矩阵断言逐行计 E:4 F:2） | 各新测试文件 |
| API 审计表 | 22 行 | 子代理素材采集中 | API_AUDIT.md |
| 原子提交 | ≥16 | 1（脚手架） | — |
| 触碰文件 | ≥14 | 5 | — |
| DECISIONS | ≥5 条 | 草案 6 条 | DECISIONS.md |
