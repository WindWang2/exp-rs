# TEST_MATRIX — F20 context-help-diagnostics-11

每项能力 → 独立 oracle → 命令 → exit → evidence。全部命令在 worktree 根执行，`QT_QPA_PLATFORM=offscreen`，测试 `-j1`。

| # | 能力 | 独立 oracle | 命令 | 基线 exit | 终态 exit (R1/R2) |
|---|---|---|---|---|---|
| T1 | HelpId/registry/搜索/内容店回归 | 既有 test_help_core（不弱化） | `ctest --test-dir build-dev -R '^test_help_core$' --output-on-failure` | 42 (10/11 case) | 0 / 0 |
| T2 | 命令 census（运行期注册表↔知识页双向） | 既有 test_command_contract_9 + 共享 command_ref_scanner | `ctest --test-dir build-dev -R '^test_command_contract_9$' --output-on-failure` | 42 (10 断言) | 0 / 0 |
| T3 | 命令 census（源码扫描盲区收敛） | test_help_coverage（改用共享 scanner） | `ctest --test-dir build-dev -R '^test_help_coverage$' --output-on-failure` | 待填 | 待填 |
| T4 | 错误码 census（harness+operator 全码 curated） | test_diagnostics_contract_9（变异测试） | `ctest --test-dir build-dev -R '^test_diagnostics_contract_9$' --output-on-failure` | 42 (7 码) | 0 / 0 |
| T5 | i18n ts 契约 + NOOP 上下文 drift gate | test_i18n（新增 gate 段） | `ctest --test-dir build-dev -R '^test_i18n$' --output-on-failure` | 0 (无 gate) | 0 / 0 |
| T6 | 帮助系统 GUI/文档回归 | test_help_system / test_global_help_tips | `ctest --test-dir build-dev -R 'test_help_system|test_global_help_tips' --output-on-failure` | 待填 | 待填 |
| T7 | DiagnosticReport 契约 | test_diagnostic_report | `ctest --test-dir build-dev -R '^test_diagnostic_report$' --output-on-failure` | 0 | 0 / 0 |
| T8 | availability facts↔enabled 零漂移 + 全命令 reason | 新增断言（挂 test_help_coverage 或独立 target） | 同 T3/新 target | — | 待填 |
| T9 | ContextualHelpResolver known-answer/negative | 新增 target（sicnu_help 纯逻辑） | `ctest --test-dir build-dev -R 'context_help' --output-on-failure` | — | 待填 |
| T10 | 生成物 zero-diff | 新增段：重生成 5 页 vs docs/generated/help 逐字节 | 同 T3 或独立 | — | 待填 |
| T11 | UX guidance corpus（错误/禁用/空数据/离线/模型缺失 × 中文 × 无 secret） | 新增 target，场景表驱动 | `ctest --test-dir build-dev -R 'ux_guidance' --output-on-failure` | — | 待填 |
| T12 | diff hygiene | `git diff --check origin/master...HEAD`；冲突标记/secret 扫描 | 见 P8 | — | 待填 |

Oracle 独立性说明：
- T8 的期望 facts 由 ContextRules（enabled 真值）派生，测试断言 adapter 输出与其一致 + 每个禁用样例 reason 非空。
- T9 known-answer 表硬编码期望（帮助 id / 动作文案存在性），不调用 renderer 自证。
- T10 期望文件是仓库内已提交 docs/generated/help/*.md。
- T11 场景期望独立写在测试内（含 secret 正则：password/token/secret/api[_-]?key 等大小写变体）。


## 终态实测（2026-09-16，Round 2 = 双跑第一遍；Round 3 于 review 后）

| 套件 | R1 | R2 |
|---|---|---|
| test_help_core | 0（2150 断言/12 case） | 0 |
| test_command_contract_9 | 0（190/6） | 0 |
| test_diagnostics_contract_9 | 0（170/6） | 0 |
| test_i18n | 0（401/6） | 0 |
| test_diagnostic_report | 0（20/3） | 0 |
| test_help_system | 0（78/5） | 0 |
| test_global_help_tips | 0（25/4） | 0 |
| test_help_coverage | 0（12338/9） | 0 |
| test_ux_guidance_corpus | 0（838/9） | 0 |

T3/T8/T9/T10/T11 的 oracle 挂载：T3/T10 在 test_help_coverage（命令 census + zero-diff case）；T8 在 availability facts 断言（test_help_coverage availability case + corpus）；T9/T11 = test_ux_guidance_corpus。均含于上表 R1/R2 exit 0。
