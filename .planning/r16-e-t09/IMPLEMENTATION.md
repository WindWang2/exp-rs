# R16-E T09 — Implementation Notes

轨道：Help 知识提示 + i18n 语言一致性（`hardening/r16-e-t09-help-i18n`，基线 46791bc65）
可写域遵守 OWNERSHIP.md：`src/help/**`（除 operator_help_provider.cpp）、`data/help/**`（除
operators/data_io.json、operators/optical.json）、`resources/translations/**`、
`scripts/audit_i18n_strings.py` + 新脚本、新测试、`tests/CMakeLists.txt` 末尾追加、
`.planning/r16-e-t09/**`。

本版为实测重写：前一会话只落盘了测试/脚本/台账，未应用任何 src/help 修复、未构建、未运行
（其台账的 GREEN 记录不可复现）。本会话补齐：全部 RED 捕获 → 修复 → GREEN×2 → 回归 → 提交。

## WP1 — 帮助数据完整性审计（`tests/test_r16_E_help_data_integrity.cpp`，5 case）

审计范围：data/help 全部 json（含 #1517 只读两文件的**只读读取**；缺陷修复不落它们）。

基线审计结果（RED 运行区分两类失败，其余子断言全过）：

| 检查 | 基线结果 |
|---|---|
| (a) id 全局唯一（原始 JSON 行扫描 + 非空泛下限 ≥300） | 干净 |
| (b) related/diagnostics/supersededBy/guidance.actions/helpTopic/relatedTools 悬挂 | 干净（合成宇宙=data/help ∪ glossary 去重 id ∪ `operator.`+census 157 个 `rs:*`；guidance.action 加 `command.` 前缀后命中） |
| (c) 可达条目 title+summary 非空 | **缺陷：163 个 curated diagnostic summary 为空** |
| (d) code↔DiagnosticCatalog::idFor↔ErrorDiagnosticsBridge::helpIdForCode | 干净（163 页全一致） |
| (d-ext) code 从失败 prose 可恢复 | **缺陷（部分）**：单驼峰 operator code 不可恢复（见下）；小写/带点 code 为设计外（生产端不进 prose） |
| docs[] 落盘存在 | 干净 |

### 修复 1（c）：`src/help/help_content_store.cpp` parseEntry Diagnostic 分支

```cpp
if ( d.summary.isEmpty() )
    d.summary = info.whatHappened.left( 120 );
```

镜像 `DiagnosticCatalog::fallback()` 的 `d.summary = info.whatHappened.left(120)`。消费路径：
HelpPresenter tooltip/statusTip（title-only 降级）、HelpCompact::summary（agent 摘要空串）、
HelpSearchIndex build（summary 权重 1.0——curated 页比 fallback 页更难被搜到）。
`HelpMarkdownWriter::diagnosticReference` 不读 summary → docs/generated 零 diff 门不受影响。

### 修复 2（d-ext）：`src/help/error_diagnostics_bridge.cpp` 候选正则

缺陷链（全部读码核实）：`RSOperatorError::what()` = `"CodeName: message"`
（rs_operator_error.cpp）→ `job_engine.cpp:1043-1060` 将 `e.what()` 原样写入任务错误文本 →
`workflow_session_controller.cpp:39-50 withDiagnosticLink` 用 `helpIdFromMessage` 扫描该文本 →
原正则 `\b[A-Z][A-Z0-9_]{2,}\b|\b[A-Z][a-z]+(?:[A-Z][a-z]+){1,}\b` 要求 ≥2 驼峰，单驼峰
code（operator 族 `Cancelled`/`Unknown`）永不命中 → curated 页
`diagnostic.operator.cancelled/unknown` 存在但从真实取消/未知错误文本不可达。

修复：正则加第三分支 `\b[A-Z][a-z]{2,}\b`。安全性：候选仍需**精确等于** curated code 表项才
解析（F20 "no guessing" 不变）；普通大写词不命中任何表项即惰性。测试 (d) 同步重塑：仅对
identifier 形（`^[A-Z][A-Za-z0-9_]+$`）code 断言 prose 可恢复；小写/带点 code（dataset/geospatial/
preflight/rs 带点）按生产端证据列为结构化-only（见 GOAL_LOOP_LEDGER 第 5 行证据链）。

## WP2 — #1489 残留收口

实测口径（与前会话一致，本会话复跑确认）：提交态 .ts 23,559 条 / 0 unfinished；残留=再生成新增
1 条（`sicnu::app::ExperimentStudioDock` / ` (preview limited to first %1 of %2 rows)`，
src/app/experiment_studio/experiment_studio_dock.cpp:472）。修复：tracked .ts 按 lupdate 插入序补
finished 翻译 ` （预览仅显示前 %1 行，共 %2 行）`。RED 证据：gate 对 HEAD 版 .ts FAIL（1 problem），
对修复后 .ts PASS ×2（直跑 + ctest runner）。

### `scripts/check_translation_freshness.py`

- lupdate 目录清单从根 CMakeLists.txt `sicnu_i18n_update` COMMAND 行解析（valued-flag 处理），
  解析失败回退 13 目录硬编码；本会话实测解析输出与 CMakeLists 一致。
- 再生成进临时文件（不改 tracked .ts）；FAIL 条件：tracked unfinished > 基线（默认 0）或再生成
  含 tracked 缺失的新消息；stale 仅 WARN。`--selftest` 用真实 lupdate + fixture 跑正反例
  （本会话 GREEN）。注册 ctest `test_r16_E_i18n_translation_freshness`（TIMEOUT 900）。

## WP3 — 解析健壮性（`tests/test_r16_E_help_parsing_robustness.cpp`，9 case）

通过公共加载面（loadFromDirectory/loadFromJson/loadFromFile/tokenize/bridge）打边界：
300 层深嵌套（terminology=RED 路径 / store=已守卫回归钉）、UTF-16LE BOM typed error、UTF-8 BOM
应可载、原始无效 UTF-8 字节无崩溃、2MiB 字段 + 1MiB 字段名限时、1e999/int64 溢出/数值 id
typed、CJK+组合字符+星面 token 化、512KB CamelCase 与 UPPER_SNAKE 过 bridge 正则限时、
5000 字符路径 typed。全部以 withinMs 预算防挂起。

RED：deep-nesting 用例未捕获 `Json::LogicError`（`Exceeded stackLimit in readValue().`）。
修复：`terminology_provider.cpp` loadFromJson 加 `builder["stackLimit"]=64`（glossary 深度 ≤4，
对齐 store 的既有选择）+ `try/catch(const std::exception&)` 转入 `result.errors`。GREEN ×2。
其余 8 case 在 master 即绿（无假缺陷）。

## WP4 — 搜索索引性能与评分稳定性（`tests/test_r16_E_help_search_perf.cpp`）

**RED（前会话预判"预期 GREEN"是错的）**：3000 查询 73,367ms > 15,000ms 上限。定位：查询 token
命中大 posting（合成语料每 summary 含"辐射/斑点/terrain"，单查询 join ~20k 条 posting 条目），
旧实现对每条做 `scores[QString key] += w` 的 QString 哈希查找（Debug ~12ms/查询），再全量
O(N logN) 排序后截断到 20。

最小优化阶梯（每步实测，见 PERFORMANCE.md）：
1. top-K `nth_element`（同全序 (score↓,id↑)，id 唯一 → 前 K 集合唯一，输出字节等价）：73.4s→36.1s；
2. (score, descriptor*) 对排序 + 仅物化前 K 个 SearchHit：无增益（36.1→38.4s，噪声）→ 真热点在 join；
3. **稠密 int 文档下标**：`m_index: QHash<QString, QHash<int,double>>` + `m_documents` 改
   QVector（id→下标表建索引期生成；重复 id 沿用旧合并语义：同槽累积、后到覆盖显示字段）；
   score 累积走 `QVector<double>` 数组下标：**73.4s→11.3s（6.5×）**，RSS 138.8→123.5MB，
   原 15s 上限原样成立（未放宽 Oracle）。确定性：累积次序只依赖查询 token 序，稠密化不改任何
   加法/比较序；测试内两次 build + 重复查询字节一致断言通过 ×2 进程。

## WP5 — #1465 terminology 回归（`tests/test_r16_E_help_terminology_regression.cpp`，4 case）

master 已含修复（e710cec86 assignedIds）。回归钉：三重重复 en 各得唯一后缀 id 且 count 保持、
不同拼写 slug 碰撞同样后缀、related→重复 en 解析首 id 无悬挂、端到端 loadFromJson 报数据级
duplicate 错误但 appendDescriptors 全量注册、en/zh/alias lookup。GREEN ×2（无 RED，符合预期）。

## WP6 — 未执行（时间允许项；机械验证项已被 WP1 (b) 的命令/工具引用审计部分覆盖）

## Cross-track spec（给 T10 / 集成）

1. **lupdate 目录清单补 `src/python`**（T10 拥有根 CMakeLists.txt:973 行）：实测加入后新增
   1 context 1 message（`SicnuPythonConsole` / `Enter Python code...`，
   src/python/sicnu_python_console.cpp:45）。T10 改行的同一提交需在 .ts 补 finished 翻译（建议
   `输入 Python 代码...`）；之后本轨道 freshness gate 自动以新清单继续把门。
2. **lupdate 静态提取缺口（记录，无需动作）**：3 处 `Cannot invoke tr() like this`——
   src/app/dialogs/stac_browser_dialog.cpp:242、src/app/widgets/roi_statistics_widget.cpp:205,305。
   字符串永不进 .ts；src/app 非本轨道域。
3. **tests/CMakeLists.txt 尾部**：本轨道追加 `# R16-E T09` 块（4 C++ target + 1 python ctest），
   与 #1515/#1517 追加式改动无语义冲突。

## 发现但未修（协调对象标注）

- `test_i18n_string_audit` gate 于 **pristine master 同样 FAIL**（tr_zh_source=4、nonui_zh
  1117>1059、src/science_context/capability_router.cpp 66>4）——临时 worktree 46791bc65 复证。
  master CI 基线 56 失败（T10 台账 run 38843229045）的组成部分；src/science_context 非本轨道
  可写域 → 留集成/后续轨道协调，**非本轨道引入**（本轨道未改任何被扫描文件）。
- `TerminologyProvider::lookup()` 对重复 en 的 m_index 指向最后一个重复项（QHash insert 覆盖）——
  非 #1465 症状（appendDescriptors 路径无恙），记录不动。
- data_io.json / optical.json（#1517 只读）未深审（只读读取已进 WP1 审计宇宙、无失败；深审
  只会制造合并噪音）；#1517 合并后可复用本轨道审计测试直跑。
- 任务书"5 条 unfinished"与基线不符（实测 0 条提交态 + 1 条再生成新增）——已在台账记录口径。

## 提交清单（本轨道，实际顺序；注册块随 WP2 提交落盘，保证该提交树含全部测试源）

1. `fix(help): curated diagnostics keep a fallback-grade summary; bridge recovers single-hump codes (R16-E T09 WP1)` — help_content_store.cpp + error_diagnostics_bridge.cpp + tests/test_r16_E_help_data_integrity.cpp
2. `fix(help): bound glossary JSON recursion and surface stackLimit as typed error (R16-E T09 WP3)` — terminology_provider.cpp + tests/test_r16_E_help_parsing_robustness.cpp
3. `perf(help): dense-document search index - 6.5x faster ranked queries (R16-E T09 WP4)` — help_search_index.{h,cpp} + tests/test_r16_E_help_search_perf.cpp
4. `test(help): pin #1465 terminology dedup semantics (R16-E T09 WP5)` — tests/test_r16_E_help_terminology_regression.cpp
5. `fix(i18n): close #1489 residual — translate the one lupdate-regenerated message + freshness gate (R16-E T09 WP2)` — .ts + scripts/check_translation_freshness.py + tests/CMakeLists.txt 追加块（注册全部 5 测试）
6. `docs(planning): R16-E T09 ledger, implementation, test matrix, performance (R16-E T09)` — .planning/r16-e-t09/**
