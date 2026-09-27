# API_AUDIT — 22 个 dataset 头文件的确定性审计表（Track 13 R4）

风险类别：B=字节敏感 / L=locale 敏感 / I=迭代序敏感 / F=浮点序敏感 / S=自指哈希。处置：修=本轨道修复（提交号见账本）／固化=用例钉死既有正确行为／豁免=记录理由不改动。基线 master 15e5c66b5 实测行号。

| # | 头文件 | 关键类型/签名（确定性相关） | 确定性风险点 | 本轨道处置 |
|---|---|---|---|---|
| 1 | annotation.h | `AnnotationRecord`(toJson/fromJson, defaulted ==)、`validateAnnotation`、`isContinuationOf` | 无：ISO8601(annotation.cpp:37,98)、confidence 双精度 JSON、无容器迭代 | 固化（WP-D 注记类码 fail-fast 用例覆盖 ingest 面） |
| 2 | dataset_fingerprint.h | `DatasetFingerprint`(toHex/digest/==)、`makeDatasetFingerprint(QJsonObject)`、`manifestFingerprintMatches` | S：`fingerprint` 字段豁免（dataset_fingerprint.cpp:18）已正确；字节敏感在**解析漏斗**不在本头 | 修（WP-A：textToJson BOM 单点容忍）+ 固化（KAV+矩阵） |
| 3 | dataset_ids.h | 六类强 id：generate/fromString/toString/== | `QUuid::createUuid()`（dataset_ids.cpp:12）进程内随机 | 豁免（E1：身份与内容解耦是设计意图） |
| 4 | dataset_manifest.h | `DatasetManifest::toJson/fromJson`、严格版本+未知字段容忍 | ISO8601(:100,154-155)；entries 保持插入序（契约序） | 修+固化（WP-E：golden v1、外版本拒绝、字节稳定往返） |
| 5 | dataset_qa_report.h | `buildDatasetQaReport`、`overallVerdict`、`verdictFromLeakageReport` | 无 IO（头文件自述）；类别序固定 | 固化（WP-C 覆盖矩阵间接覆盖 verdict 路径） |
| 6 | dataset_quality.h | `computeComposition`（7×QHash）、`imbalanceFindings`、`recommendQualityLevel` | QHash 仅汇入 QJsonObject（键排序，安全）；`resolutionKey` 'f',2 C locale（:24）；消息文本有损 .arg(area)（:229） | 豁免（E3 消息文本）+ 固化（键排序汇出） |
| 7 | dataset_store.h | stage/commit、`saveSplitManifest`、分页与列表查询 | 全部列表查询 ORDER BY 钉序（:393,:932,:952,:989,:1270,roword）；commit 指纹=canonical（:803-807） | 固化（WP-A 漏斗往返、WP-E golden 文本漏斗） |
| 8 | dataset_store_impl.h | `jsonToText`(Compact)、`textToJson`、`canonicalId`、StoreStmt | **B：ingest 漏斗无 BOM 容忍（红）**；jsonToText≠RFC8785（口径见 R6） | 修（WP-A：单点 BOM 剥离）+ 豁免（R6 口径分歧文档化） |
| 9 | dataset_types.h | 17 个封闭枚举 + 严格 ToString/FromString 对 | 表驱动 QHash 仅按精确键查找（dataset_types.cpp:50-51），迭代序不影响结果 | 固化（词汇 round-trip 既有用例 + 矩阵引用） |
| 10 | dataset_version.h | `DatasetVersionRecord`、`DatasetVersionDiff`、`diffManifests` | **I：diff 向量 QHash 序（dataset_version.cpp:81,89，红）** | 修（WP-E A1：按 (kind,refId) 排序）+ 固化（双跑字节稳定） |
| 11 | deterministic_random.h | `SplitMix64::next`、`Pcg32::nextU32/nextBounded/nextDouble/shuffle`、`hashSeed`、`seedFor` | 无——本头即是确定性来源；Fisher-Yates 固定遍历序 | 固化（WP-B：公开参考向量+派生向量+命名空间纪律） |
| 12 | feature_table.h | `FeatureSet::schemaDigest(excludeMetadata)`、`joinFeaturesBySampleId` | **I：缺失必需列报告取 QSet 首个（feature_table.cpp:265-279，红）**；schemaDigest 自指豁免正确（:175-184） | 修（WP-C A2：定序收集）+ 固化（digest 豁免矩阵） |
| 13 | fold_audit.h | `FoldAuditor::auditFolds`、`verifyDeterministicReplay`、`FoldComparabilitySummary` | QSet 派生列表后 .sort()（fold_audit.cpp:134-135，安全）；replay 比较内容指纹（:176） | 固化（WP-C：fold 材料化+honest gaps+replay） |
| 14 | foundry_service.h | `inspectVersion`、`versionLineage`、`runQa`、`joinFeatures` | 委托薄层；inspect 的独立 parse 点（:68）绕过漏斗 | 修（WP-A：改走 textToJson 同一漏斗，单点收敛） |
| 15 | label_schema.h | `LabelSchema::validate/ancestorsOf/leafCodes`、`LabelMapping::map/covers` | 线性 QVector 扫描为主；validate 首个重复按 QHash 序报（label_schema.cpp:188-197）仅消息序 | 修（WP-D：入库口类码对照 + 消息定位）+ 豁免（消息序） |
| 16 | leakage_audit.h | `LeakageAuditor::audit`、`LeakageReport`、`crossSplit`、kAllChecks 13 项 | QHash 桶枚举被终排序归一（leakage_audit.cpp:620-627）；auditedChecks 固定表序 | 固化（WP-C：13 检查覆盖矩阵+输入排列不变性） |
| 17 | patch_generator.h | `PatchGeneratorConfig::toJson/fromJson/configHash`、`generate` | configHash=makeDatasetFingerprint（:125-128）；seed 整数序列化（:80）；seedFor 双 purpose | 固化（WP-B seedFor 向量覆盖 patch.* purpose） |
| 18 | sample_catalog.h | `querySampleCatalog`（limit 硬顶 500）、`summarizeSampleCatalog` | 输入序稳定单遍（sample_catalog.cpp:62-70）；summary QHash 汇出经 QJsonObject 键排序 | 固化（引用于覆盖矩阵的样例查询路径） |
| 19 | sample.h | `PixelWindow`、9 载荷 struct、`SampleRecord::toJson/fromJson`、`validateSample` | **载荷校验缺口：Point 非 finite、Pixel 负值、Pair 空/自配对、Object 空 ref、Polygon 非法 WKT（红）**；WKT 足迹 'g',17 往返精确（sample.cpp:41-44） | 修（WP-D：六类非法载荷拒收 + 消息含 id+字段） |
| 20 | sample_promotion.h | `SamplePromoter::promote*`、`PromotionReport` | `requireKnownClasses` QSet 序报首个未知码（sample_promotion.cpp:80-87），仅消息序 | 豁免（判定稳定；消息序按 R5 家族原则记录） |
| 21 | split.h | `SplitConfig::toJson/fromJson/validate`、`SplitManifest`、`splitManifestFingerprint`、`SplitEngine::generate` | **seed_hex 缺失静默落 0（split.cpp:511，红）**；指纹剥 5 个易变字段（:750-755）正确；Hare-Niemeyer 固定 tie-break（:98,107-108） | 修（WP-B D1：缺失即拒）+ 固化（双跑 digest、seed=0、KAV） |
| 22 | wkt.h | `parseWktPoint/parseWktPolygon/parseWktBounds`、`SimplePolygon` | QString::toDouble C locale（wkt.cpp:34-35）；无序 hazards | 固化（WP-D Polygon 拒收复用本解析器，不另起炉灶） |

**汇总**：修 7 行（#2,4,8,10,12,15,19,21 — 含 WP-A×2、WP-B×1、WP-C×1、WP-D×2、WP-E×1）；固化 12 行；豁免 4 处（#3,6,20,#8 口径分歧/R6+E1+E3）。无 locale 敏感点（全仓 0 处 QLocale，QDateTime 全 ISO8601，double 全 C-locale 通道——独立审计核实）。
