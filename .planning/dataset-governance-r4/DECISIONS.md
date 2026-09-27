# DECISIONS — Dataset Governance Determinism Rules (Track 13 R4)

后续所有数据产物必须遵守的标准。每条规则与代码交叉核对（锚点为规则生效处的实测行号，master 15e5c66b5 基线）。

## R1 — 指纹 = canonical 语义身份，与检出字节无关

数据集指纹是**解析后语义载荷**的函数，绝不是字节拼写（EOL/BOM/缩进/尾随换行/JSON 键序/数字拼写）的函数。同语义不同检出的输入对必须同指纹。
- 入口：`makeDatasetFingerprint`（dataset_fingerprint.cpp:15-22）——剥 `fingerprint` 字段后 `canonicalizeJsonRfc8785` → SHA-256。
- 字节级检出差异在**解析层吸收**：实测 `QString::fromUtf8` 解码时即剥前导 UTF-8 BOM，`QJsonDocument` 把 EOL/缩进当作数间空白——因此 ingest 漏斗 `textToJson`（dataset_store_impl.h:71）**无需归一化源码改动**（WP-A 的诚实 no-op：矩阵固化行为优于装饰性归一）。NUL 在 token 之间被视为空白、字符串内的 NUL/截断文档在指纹上**永不等价于完好载荷**（矩阵 binary 行钉死：坏文档只能解析失败或指纹不同，二者皆合法，等同即违规）。
- 边界（负向规则）：**字符串值内部**的拼写差异（如路径分隔符 `a/b` vs `a\b`）是内容差异，指纹必须不同——git 检出过滤从不改写字符串内部。

## R2 — canonical 形态只有一个实现，一个口径

canonical JSON 只由 `canonicalizeJsonRfc8785`（execution_fingerprint.cpp:184；键按 UTF-16 码元序排序、JSON 转义、0 归一、整数直写、最短往返小数）产生。任何新的指纹/摘要需求必须复用它，禁止私有 canonical 变体。
- 已知偏差豁免：该实现的 'g' 指数格式与 ES6 Number::toString 不逐字相同（如 `1e-07`）——这是本仓 canonical 契约的既定形态；已知答案向量以**本仓 canonical 形态 + 独立 SHA-256 实现**为真值（test_dataset_fingerprint_determinism.cpp 的 KAV），不以 RFC 8785 参考实现逐字为真值。改动此口径 = 翻动全部既有 pins，必须走评审并更新 tripwire 常量。

## R3 — 随机性只有一条来源，种子派生按用途命名空间化

dataset 路径上的所有随机行为只来自 `deterministic_random.h`（SplitMix64/Pcg32，算法钉死在该文件）；`std::mt19937/std::shuffle/random_device/rand()` 禁用（标准不钉算法，跨平台不可复现）。每个随机消费点用 `DeterministicRandom::seedFor(rootSeed, purpose)` 派生独立种子，purpose 全部枚举：`split`（split.cpp:833,1087）、`patch.random`/`patch.stratified`（patch_generator.cpp:287,304）。新增随机步骤必须新增 purpose 名，绝不复用既有 purpose（否则扰动既有步骤的抽签序列）。
- 分组/分桶源必须先经 QMap（有序）再喂固定算法 shuffle（split.cpp:857,873,910,1038,1051,1129,1212 已核）。

## R4 — 种子契约：0 合法，缺失必拒

`SplitConfig::seed` 是必填项：`seed_hex` 显式 `"0"` 合法；JSON 缺失 `seed_hex` 键是操作员错误，必须以 `dataset.split_invalid` 响亮拒绝，绝不静默落 0（否则遗忘种子的切分与显式 0 切分不可区分，且制造退化可复现切分）。边界在反序列化口（`SplitConfig::fromJson`），结构体默认值不承担该语义。

## R5 — 审计器只声称它审过的，输出顺序是契约

- `LeakageReport` 只能声称 `auditedChecks()` 里列出的检查（leakage_audit.cpp:271-294：证据不支持的检查从声称集中剔除）；honest gaps（`digestUnknownCount`、fold 摘要 `notes`/`classesMissing*`）必须量化未覆盖面，不许静默。
- findings 的**输出顺序**按 (sampleA, sampleB, kind) 全序（leakage_audit.cpp:620-627），不依赖 QHash 桶序（Qt6 哈希种子逐进程随机）；同容器家族的迭代序敏感点一律终排序或定序收集。diff 向量按 (kind, refId) 排序（dataset_version.cpp，R4 修复 A1）。

## R6 — 存储文本摘要与指纹是两种口径，读者不得混用

已提交 manifest 的 `manifest_json` 列存 QJsonDocument::Compact 文本（dataset_store.cpp:806-810），其指纹列存 RFC 8785 canonical 摘要——两者**设计上不同字节形态**。store 侧幂等键/内容摘要（splits contentDigest、label schema digest）哈希的是存储文本，不是 canonical 形态：确定性在固定 Qt 序列化下成立；读者不得拿存储文本重算指纹，也不得拿指纹当文本摘要。改任一口径都是显式迁移，不是顺手重构。

## R7 — 错误定位契约（数据域诊断可机器解析）

数据域 ingest 拒绝（样本校验、注记类码、切分配置）的错误信息必须含**记录 id 与字段名**（如 `sample <id>: <field> ...`、注记拒绝须含非法类码文本），使上游能机器解析定位。诊断码沿用既有族（`dataset.sample_invalid` / `dataset.split_invalid` / `dataset.conflict` / `dataset.manifest_version`），不新造平行码族。

## 豁免清单（明确不改的既有行为）

- E1 `generate()` 的 UUIDv4 身份（dataset_ids.cpp:12）是进程内随机——身份与内容解耦是设计意图，不在确定性约束内。
- E2 诊断消息内的 `.arg(area)` 六位有损格式（dataset_quality.cpp:229-230）仅影响消息文本，不影响判定；不为此翻动既有消息快照。
- E3 `LabelSchema::validate` 与 `requireKnownClasses`（sample_promotion.cpp:80-87）的"首个重复/未知码"消息按容器序取——判定稳定、仅文本序；已按 R5 原则记录，不强制改（修改会翻动消息断言且收益为零）。
- E4 治理镜像的批量引用路径（`addRunOutputs`/`addLineageEdges`，governance_store.cpp）不查两端存在性——bulk 导入可先边后实体（镜像语义）。一致性是**报告**责任（test_data_governance_consistency.cpp 已证引用枚举可暴露悬挂），不是写入闸。
- E5 `sicnu_agent.so` 引用 `agent_loop::VerificationReport::aggregate` 而不自链 `sicnu_agent_loop`——master 上所有 sicnu_add_test 闭包断链。修在测试链接层（tests/CMakeLists.txt 宏内 WHOLE_ARCHIVE 追加，本轨道白名单内的唯一入口）；`src/agent` 的 PUBLIC-link 上游修复建议已写入提交与 PR（src/agent 在白名单外）。
