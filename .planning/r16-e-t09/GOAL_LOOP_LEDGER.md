# R16-E T09 — Goal/Loop Ledger（Oracle 判定流水）

轨道：R16-E-T09（Help 知识提示 + i18n 语言一致性）
分支：`hardening/r16-e-t09-help-i18n`（基线 origin/master=46791bc65）
工作树：`/home/kevin/project/exp-rs-r16-e-t09`
执行日：2026-10-17/18（UTC）；本文件为**实测重写版**——前一会话的台账曾记录未实际执行过的
"GREEN×2"（其 src/help 修复未落盘、WP4 从未运行）；本版每行都有本会话真实命令与输出对应。

RED→GREEN 纪律：修复前先有真实失败证据（本会话运行捕获）；关键验证 GREEN ×2。
运行环境：`QT_QPA_PLATFORM=offscreen`，`LD_LIBRARY_PATH=/home/kevin/wp2-prefix/usr/lib`
（QGIS 链运行时库前缀，本机 /usr 无 libproj/libodbc 等），Debug 构建，ctest -j1。

| # | WP | Oracle 命令 | 判定 | 证据 |
|---|----|------------|------|------|
| 1 | WP2 | `python3 scripts/check_translation_freshness.py --selftest` | GREEN：正例 PASS、负例（新增消息）检出、负例（unfinished 超基线）检出，exit 0 | 终端输出（本会话） |
| 2 | WP2 | 同脚本 gate 对 `git show HEAD:…sicnu_zh_CN.ts`（修复前 .ts） | **RED**：`FAIL: new untranslated message: context='sicnu::app::ExperimentStudioDock' source=' (preview limited to first %1 of %2 rows)'`（23,559 vs 再生成 23,560 条） | /tmp/t09-head.ts 运行输出 |
| 3 | WP2 | 同脚本 gate 对工作树 .ts（含新增 1 条翻译） | GREEN run1：`FRESHNESS GATE: PASS (unfinished=0<=0, new messages=0)`，exit 0；ctest `test_r16_E_i18n_translation_freshness` Passed 3.68s + 3.63s（GREEN×2，经 ctest runner） | 终端 + ctest 输出 |
| 4 | WP1 | `./build-r16-e-t09/test_r16_E_help_data_integrity`（master 代码，构建后首跑） | **RED**：2 case 失败——(c) floor：163 个 curated diagnostic `empty summary`；(d) bridge：`code Cancelled/Unknown/label.conflict/... not recoverable from failure text`（163 页中多族失败），exit 42 | 本会话完整输出（head/tail 各 60/30 行留档） |
| 5 | WP1 | 证据判定（读码，非运行）：(d) 的失败分两类——(i) 小写/带点 code（dataset `label.*`、geospatial `snake_case`、preflight、`rs` 带点 UPPER）在生产端**从不进 prose**（dataset_quality 的 code 与人话分离；GeoError::what() 仅 message；preflight code 只驱动结构化 proposal；rs 带点 code 无发射点）→ 对它们断言 prose 可恢复性是**测试超纲**，重塑为仅对 identifier 形（大写开头 `[A-Z][A-Za-z0-9_]+`）断言；(ii) 单驼峰 `Cancelled`/`Unknown`：`RSOperatorError::what()`=`"CodeName: message"` 且 `job_engine.cpp:1052-1060` 把 what() 原样写入任务错误文本 → `withDiagnosticLink`→`helpIdFromMessage` 实际消费路径扫不到 = **真实缺陷**（src/help/error_diagnostics_bridge.cpp 候选正则要求 ≥2 驼峰） | src/jobs/job_engine.cpp:1043-1060、src/operators/framework/rs_operator_error.cpp what()、src/app/shell/workflow_session_controller.cpp:39-50 |
| 6 | WP1 | 修复后重跑（help_content_store.cpp Diagnostic 分支补 summary 默认值 = fallback() 镜像；bridge 正则补单驼峰分支；测试 (d) 重塑） | GREEN ×2：`All tests passed (22 assertions in 5 test cases)` 两次，exit 0 | 本会话两次运行 |
| 7 | WP3 | `./build-r16-e-t09/test_r16_E_help_parsing_robustness`（master 代码） | **RED**：deep-nesting terminology 用例 `FAILED: due to unexpected exception with message: Exceeded stackLimit in readValue().`（Json::LogicError 逃逸 typed 边界），8/9 case 过、exit 42 | 本会话输出 |
| 8 | WP3 | terminology_provider.cpp loadFromJson 补 `stackLimit=64` + try/catch 后重跑 | GREEN ×2：`All tests passed (68 assertions in 9 test cases)` 两次，exit 0 | 本会话两次运行 |
| 9 | WP4 | `./build-r16-e-t09/test_r16_E_help_search_perf`（master 代码） | **RED**（前台账预测"预期 GREEN"错误）：`perf 3000 queries: 73367 ms` > 15000ms 上限，exit 42 —— 真实低效：score join 逐 posting 条目 QString 哈希（~20k/查询）+ 全量排序 | PERFORMANCE.md 阶梯表第 1 行 |
| 10 | WP4 | 优化阶梯实测（top-K nth_element → pair 排序 → 稠密 int 下标） | 稠密下标版 GREEN ×2：3000 查询 11273ms / 10802ms（基线 73367ms，6.5×），exit 0 ×2；确定性断言（两次 build + 重复查询字节一致）内嵌通过 | PERFORMANCE.md 全表 |
| 11 | WP5 | `./build-r16-e-t09/test_r16_E_help_terminology_regression` | GREEN ×2：`All tests passed (35 assertions in 4 test cases)` 两次（master 已含 e710cec86 修复；纯回归钉，无 RED 预期） | 本会话两次运行 |
| 12 | 回归 | test_i18n / test_help_commands_schema / test_i18n_migrated_translations / test_help_core（重链 Sicnu::help 的既有测试） | 全 GREEN：401 / 13 / 2 / 2156 assertions passed | 本会话运行输出 |
| 13 | 回归 | `ctest -R "^test_r16_E_" -j1`（5 测试经标准 runner） | GREEN：`100% tests passed, 0 tests failed out of 5`，31.29s | 本会话 ctest 输出 |
| 14 | 基线 | `scripts/audit_i18n_strings.py` gate（i18n_string_audit） | **FAIL 于 pristine master 同样输出**（tr_zh_source=4、nonui_zh=1117>1059、capability_router.cpp 66>4）——用临时 worktree 46791bc65 复证 → master 既有基线漂移，非本轨道引入；src/science_context 不在本轨道可写域，记录留协调 | /tmp/t09-master-check 同输出对比（worktree 已清理） |

## 证据关键事实（先于实现固定，本会话核实）

- jsoncpp（系统 1.9.x）：默认 stackLimit=256，越限**抛异常**；skipBom 默认 true（UTF-8 BOM 可过）；
  UTF-16LE BOM = 语法错误（typed）；无效 UTF-8 原始字节被接受（转 QString 得替换符）；1e999 不抛。
  `HelpContentStore::loadJsonFile` 已有 stackLimit=64+try/catch（#1447）；`TerminologyProvider::loadFromJson`
  原无守卫——WP3 RED 实锤（见第 7 行，非探针推断）。
- 提交态 `sicnu_zh_CN.ts`：23,559 条 message / 0 条 unfinished（任务书"5 条 unfinished"与
  46791bc65 不符，如实记录）；真实残留=按 CMakeLists.txt:973 十三目录再生成时新增 1 条
  （ExperimentStudioDock 预览行），已补译。
- WP1 数据侧：id 全局唯一（685 顶层）、related/diagnostics/supersededBy/guidance.actions/helpTopic/
  relatedTools 在合成宇宙（data/help ∪ glossary ∪ rs:* 源扫描 census 157）内全部可解析、
  diagnostics code↔idFor 一致、docs[] 落盘存在——这些子断言在 RED 运行中即已通过（失败仅
  floor 与 bridge-prose 两类）。
