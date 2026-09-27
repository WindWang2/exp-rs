# COMMAND_MATRIX — 19/19 顶层命令三件套总表（Track 14 WP-G 收口）

三件套 = 退出码合同用例 + 错误路径结构化用例 + help/词表对齐。
追溯：每行的"退出码用例/错误路径用例"列给出 ctest 测试名内的 SECTION/TEST 名（评审可抽 5 行核对 ctest 输出）；提交号列给出该命令主要落点的原子提交。

| 命令 | 退出码用例（ctest 可数） | 错误路径用例（结构化） | help/词表状态 | 主要提交 |
|---|---|---|---|---|
| algorithms | usage_errors: "algorithms schema with no id"(6)；unknown_resources: "algorithms schema…"（5）；fail_fast: "algorithms bogus-sub"(6)；parity: search shape/limit(6/0) | 4 元组：unknown algorithm/unknown subcommand（details.expected=list\|search\|schema） | README 行 ✓；词表双向（docs→CLI 方向 1）✓ | 5bd878d7a5 |
| run | usage_errors: "run with no operator id"(6)；unknown_resources: "run…"（5） | 4 元组 usage hint（usageError）；unknown algorithm details | README 行 ✓（无子命令，N/A 论证在 CONTRACT §2） | 5bd878d7a5 |
| pipeline | usage_errors: "pipeline run"(6)；unreadable: "pipeline run missing"(6)；schema: "pipeline validate bad"(2)；fail_fast: 未知子命令(6) | 缺文件 details(actual=path)；unknown sub details(expected=run\|validate\|resume) | README 行 ✓；词表方向 1 ✓ | addfbc4f01 |
| workflow | usage_errors: "workflow bogus-sub"(6)；unreadable: missing file(6)；schema: bad doc(2) | usage hint；词表在 usage 行（validate\|run\|list-runs\|resume） | README 行 ✓；词表方向 1 ✓ | 5bd878d7a5 |
| plugin | usage_errors: "plugin"(6)；unknown_resources: "plugin inspect…"（5）；uninstall 未知 id(5) | uninstall details（expected/diagnostics）；7 个 usage hint | README 行 ✓；词表方向 1 ✓ | 5bd878d7a5 |
| models | fail_fast: "models bogus-sub"(6)/"models inspect 无名"(6)；unknown_resources: "models inspect…"（5） | unknown sub details(expected=list\|inspect) | README 行 ✓；词表方向 1 ✓ | 5bd878d7a5 |
| project | usage_errors: "project info"(6)；fail_fast: 未知 governance 子命令(6，加载前)；schema: validate/health 报告不 ok(2) | unknown sub details(expected=10 词表)；migrate/relink/export/import→3（StructuredError 同 finish 出口） | README 行 ✓；词表方向 1（10 子命令）✓ | 752ad7dae0 |
| data | usage_errors: "data"(6) | usageError×3（data/cube/mirror usage 行内全词表） | README 行 ✓；词表方向 1（11 子命令）✓ | 5bd878d7a5 |
| data-providers | （N/A：唯一路径为列表，见 CONTRACT §2 行 9） | （N/A 同上） | README 行 ✓（无子命令） | 7dff299ed5（基线记录） |
| dataset | usage_errors: "dataset"(6)；exit_codes: store-open 失败(6)/缺 name(6)/version 不存在(5)；fail_fast: 副作用零×2；parity: create→stats round-trip(0) | 4 元组×24 位点（fail() 单点构造；expected=10 子命令词表） | README 行 ✓；词表方向 2 **精确集合相等** ✓ | 406dd2659e |
| experiment | usage_errors: "experiment"(6)；fail_fast: 缺 name 副作用零 | 4 元组×11 位点（expected=5 子命令词表） | README 行 ✓；词表方向 2 精确 ✓ | 406dd2659e |
| reproduce | usage_errors: "reproduce inspect"(6)；fail_fast: export/validate 副作用零×2；2 类=validate 不可满足 | 4 元组×8 位点（expected=3 子命令词表） | README 行 ✓；词表方向 2 精确 ✓ | 406dd2659e |
| lab | schema: "lab --lab 缺值"(2)；fail_fast: --max-bytes/--max-submissions 非整数(2) | 4 元组×26 位点（labError/labFlagError→finish 信封） | README 行 ✓（flag 型，N/A 论证在 CONTRACT 行 13） | 90e04432ee |
| env-doctor | usage_errors: "env-doctor --bogus"(6)；2 类=环境 errorCount>0（机器相关，不做进程断言，CONTRACT 行 14） | unknown argument details(expected=--json, actual) | README 行 ✓（flag 型） | a3dfd12d2d |
| tools | usage_errors: "tools schema"(6)；unknown_resources: "tools schema 未知"(5) | unknown sub details(expected=list\|search\|schema) | README 行 ✓；词表方向 1 ✓ | 5bd878d7a5 |
| batch | usage_errors: "batch validate"(6)；unreadable: missing manifest(6)；2 类=validate schema 坏 | unknown sub details(expected=run\|validate) | README 行 ✓；词表方向 1 ✓ | addfbc4f01 |
| session | usage_errors: "session 未知 action"(6)；7 类=driver 不可用（CONTRACT 行 17） | 4 元组×5 位点（expected=14 动作词表） | README 行 ✓（本轮新增行）；词表 session 专测 ✓ | d3524ab9e6 |
| passport | usage_errors: "passport"(6)；unreadable: 缺文件(6)；2 类=diff 文档无效 | 4 元组×6 位点（passportError） | README 行 ✓（flag 型） | d3524ab9e6 |
| catalog | usage_errors: "catalog bogus-sub"(6) | usage hint（usageError×2） | README 行 ✓；词表方向 1 ✓ | 5bd878d7a5 |

**覆盖核对（下限对照）**：
- 19/19 命令有退出码用例或书面 N/A（CONTRACT §2）✓
- 退出码用例 ≥19：usage_errors 16 SECTION + unknown_resources 5 + unreadable 4 + schema 3 + fail_fast 13 + parity 12（跨 7 个测试目标，同名 SECTION 可数）✓
- 错误路径四元组 ≥20：dataset 24 + experiment 11 + reproduce 8 + lab 26 + passport 6 + session 5 + dispatch TU 17 usage + tools/batch/pipeline/project/algorithms/models/plugin/catalog/env-doctor 各 1-3 ≈ **110+ 位点**（任务书下限的 5 倍）✓
- help 对齐：README 19 行（session 本轮补）+ 词表双向测试 ✓
- 提交列：每行可追溯原子提交（git log 可审计）✓
