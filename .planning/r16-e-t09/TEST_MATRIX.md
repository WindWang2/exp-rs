# R16-E T09 — Test Matrix

C++ 测试目标轻链（Catch2::Catch2WithMain + Qt6::Core + Sicnu::help；WP2 为 python ctest），
注册块追加于 `tests/CMakeLists.txt` 末尾（`# R16-E T09` 注释块，只追加未动既有行）。

运行环境：`QT_QPA_PLATFORM=offscreen`、`LD_LIBRARY_PATH=/home/kevin/wp2-prefix/usr/lib`、
Debug 构建、ctest `-j1`、构建 `-j4`（CMAKE_BUILD_PARALLEL_LEVEL=4）、跨轨道锁
`/home/kevin/project/exp-rs-r16-build.lock`。

| 测试 | WP | 覆盖 | RED 证据（本会话实测） | GREEN ×2 |
|---|---|---|---|---|
| `test_r16_E_help_data_integrity` | WP1 | (a) id 全局唯一（原始 JSON 行扫描）；(b) related/diagnostics/supersededBy/guidance.actions/helpTopic/relatedTools 在合成宇宙（data/help ∪ glossary ∪ rs:* 源扫描 census，REQUIRE ≥100 非空泛）内可解析；(c) 非 command 条目 title+summary 非空；(d) family/code↔idFor↔helpIdForCode 全一致 + identifier 形 code 从 prose 可恢复（bridge）；docs[] 落盘存在 | floor：163 empty summary；bridge-prose：Cancelled/Unknown 等多族（exit 42） | ✅ 22 assertions / 5 cases ×2 |
| `test_r16_E_help_parsing_robustness` | WP3 | 深嵌套（terminology=RED 路径；store=守卫回归钉）、UTF-16LE BOM typed、UTF-8 BOM 可载、无效 UTF-8 无崩溃、2MiB 字段+1MiB 字段名、数值越界 typed、CJK+组合字符+星面 tokenize、512KB 正则敌意文本过 bridge、超长路径 typed | deep-nesting：`unexpected exception: Exceeded stackLimit in readValue().`（exit 42） | ✅ 68 assertions / 9 cases ×2 |
| `test_r16_E_help_search_perf` | WP4 | 5000 描述符固定种子：两次独立 build + 重复查询 (id,17 位 score) 字节一致；3000 查询 <15s；1MiB CJK tokenize <10s；exact-id 自排第一 | **perf 3000 queries: 73367 ms > 15000**（exit 42）——真实低效（见 PERFORMANCE.md） | ✅ 3000 查询 11273/10802 ms ×2（exit 0） |
| `test_r16_E_help_terminology_regression` | WP5 | #1465：N 重重复 en 唯一后缀 id、slug 碰撞后缀、related 解析首 id、e2e 数据级报错+组合不丢、en/zh/alias lookup | 无 RED（master 已修，回归钉） | ✅ 35 assertions / 4 cases ×2 |
| `test_r16_E_i18n_translation_freshness`（python ctest） | WP2 | lupdate 参数镜像 CMakeLists 再生成→临时 .ts；tracked unfinished>0 或新消息缺翻译 → exit 1；stale WARN；--selftest 正反例 | gate 对 HEAD .ts：FAIL 1 problem（ExperimentStudioDock 新消息） | ✅ 直跑 PASS + ctest Passed 3.68s/3.63s |

## 既有测试回归（本会话实测，全部通过）

| 测试 | 链 | 结果 |
|---|---|---|
| `test_i18n` | Catch2+Qt6::Core+Xml+Sicnu::help（含 HelpSearchIndex 消费） | 401 assertions PASS |
| `test_help_commands_schema` | Catch2+Qt6::Core | 13 assertions PASS |
| `test_i18n_migrated_translations` | Catch2+Qt6::Core | 2 assertions PASS |
| `test_help_core` | Catch2+Qt6::Core+Sicnu::help+qrc（terminology/search/store 主钉） | 2156 assertions PASS |
| `ctest -R "^test_r16_E_" -j1` | 5 测试标准 runner | 100% passed, 31.29s |

未跑（重链 qgis_core/gui，时间/锁预算外）：test_help_system、test_help_coverage、
test_help_integrity_12、test_capability_knowledge、test_global_help_tips——风险已由
test_help_core（同一 Sicnu::help 库的主钉）+ test_i18n 覆盖大部分语义；如实注明。

已知基线漂移（非本轨道）：`test_i18n_string_audit` gate 于 pristine master 同 FAIL
（src/science_context/capability_router.cpp 等；临时 worktree 复证，见 IMPLEMENTATION.md）。
