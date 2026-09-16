# BASELINE — F20 Context Help, Diagnostics & UX Guidance 11.0

Phase 0 只读审计完成于 2026-09-16（本地时间），全部基于 `git fetch origin --prune` 后的事实。

## 仓库状态

- `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`（"fix: fail-closed fixes for review issues #994–#999 (#1000)"）。
- 本 track worktree/branch 基于此 SHA 创建：`zcode/context-help-diagnostics-11`。
- Prompt 快照中的 `ebcafb4d02` 已过时（快照后合入 #991/#992/#993/#1000 等）。

## Open PRs（启动时）

| PR | branch | mergeable | 与本 track 交集 |
|---|---|---|---|
| #1008 feat(spectral): Day 13 radiometric calibration, 6S atmospheric correction & spectral workbench | `zcode/radiometric-spectral-workbench` | **CONFLICTING** | 无 primary-scope 交集（src/app/widgets/spectral_*、src/agent/spatial_tools、src/analysis、src/core/radiometric_state 等）；共享 integration 文件仅 .gitignore/tests CMakeLists |
| #1009 feat(execution-11): scientific execution runtime convergence | `zcode/execution-runtime-convergence-11` | **MERGEABLE** | **`data/help/diagnostics.json`**（追加 2 个 operator 页：CorruptArtifactData / ResourceBudgetExceeded，纯文件尾 append）、`src/operators/framework/rs_operator_error.{h,cpp}`（追加 2 个错误码）、`CHANGELOG.md`、`.gitignore`、`tests/CMakeLists.txt` |

`gh pr diff 1009 -- data/help/diagnostics.json` 实际 diff = 47 行新增（2 条 operator 诊断页，全部位于 JSON 数组**末尾**）。rs_operator_error 变更 = +8 行（2 个枚举值 + toString 分支）。

## Open Issues（启动时全部 7 个）

#1001（io:clip CRS）、#1002（workflow registry fail-open）、#1003（dataset join null）、#1004（dataset:qa scan_capped）、#1005（georef CRS transform）、#1006（workflow syntheticExecute）、#1007（dataset:qa CRS audit）。
**全部为 dataset/workflow/io/georef 域 bug，不在本 track primary scope**；本 track 不认领、不修复，仅记录 dedupe。

## ISSUES.md 状态

ISSUES.md 是旧 D3 算子缺口 backlog（rs:temporal_* / rs:sar_* / rs:spectral_* / cartography:* 管道化），与帮助体系无关。**不作为本 track backlog**。

## 近期帮助层相关 commit（git log -- src/help data/help）

- `ad0c9130c3` feat(agent): capability knowledge + help descriptors for CN import operators
- `08826ec8ad` fix(help): surface PolicyRefused and workflow commands in curated catalogs
- `a243461cc1` fix(i18n): regenerate zh_CN.ts against the repaired sources
- `8df5111b6c` feat(help): TerminologyProvider — unified RS glossary loader
- `52b1d4c84a` feat(help): derive parameter help from operator schemas, unify diagnostics, add GUI/CLI/MCP projections
- `b7ef5afdf6` feat(help): establish help descriptor registry and stable IDs

## 帮助体系现状摘要（详见 CURRENT_ARCHITECTURE.md）

- `src/help/`（独立库 `sicnu_help`，仅依赖 Qt Core/data/jsoncpp，有 CMake 禁链守卫）：HelpId 稳定 ID、HelpDescriptor、HelpRegistry（单例+alias+引用校验）、HelpContentStore（递归加载 `:/help` qrc 资源）、composeHelpSystem（JSON+术语+Command/Operator provider 四源合并）、HelpSearchIndex、HelpPresenter/HelpTopicText/HelpMarkdownWriter、DiagnosticCatalog、TerminologyProvider、AvailabilityFacts。
- `data/help/`：commands.json(59)、concepts.json(12)、diagnostics.json(104)、workbenches.json(8)、operators/*.json(7 文件 109 条)、help_content.qrc（打包上述+terms/rs_glossary.json 307 条术语）。
- Surface：GUI（F1 HelpEventFilter→HelpCenterDialog、HelpViewerDialog=USER_GUIDE、SicnuDialogHelp、schema_form_builder What's This）、CLI（--operator-help/--help-topic/--list-topics/--export-help-docs）、Agent（MCP get_tool_help、LabDiagnosis.helpId）。
- 测试：test_help_core、test_help_coverage、test_help_system、test_global_help_tips、test_i18n、test_diagnostics_contract_9、test_command_contract_9、test_diagnostic_report（均注册于 tests/CMakeLists.txt）。

## Phase 0 发现的缺口（证据级，待测试运行确认为红/绿）

1. 10 个已注册命令疑似无 help 知识：cartography.compose/export/preflight/repair、workbench.cartography/classifyStudio/georefDual/ir2Pipeline/operatorCatalog/visualAnalytics（command_defs.cpp RS_CMD 注册 vs commands.json 59 条 diff 推导）。
2. test_help_coverage 的命令扫描器前缀集合缺 `workflow`，且 idiom 不容 `= "…"\n);` 变体（test_help_coverage.cpp:75,94）。
3. 7 个新 harness 错误码（WAVELENGTH_INCOMPATIBLE/TEMPORAL_MISALIGNMENT/CATEGORICAL_MISMATCH/RESOURCE_OVER_BUDGET/OUTPUT_PATH_COLLISION/NONDETERMINISTIC_CHAIN/FACT_CONFLICT，a966534ef5 引入）疑似无 curated 诊断页。
4. diagnostics.json `diagnostic.operator.runtime_provider_failed` 的 `"retry": "retryable"` 不被 help_content_store 解析（只认 none/manual/transient，其余静默降级）。
5. GUI 错误路径未接 DiagnosticCatalog（唯一生产消费方是 agent/lab_diagnostics）。
6. docs/generated/help/*.md 已提交但无 zero-diff gate（仅内存内两次相等断言）。
7. #983 的 SicnuDialogHelp QT_TRANSLATE_NOOP 上下文未进 zh_CN.ts（ts 再生成早于 #983），运行时仍英文；无 "NOOP 上下文全部入 ts" gate。
8. help presenter/topic_text 骨架文案硬编码中文未包 tr()；data/help JSON 中文单语。
9. AvailabilityFactsAdapter 静态表仅 24 命令，表外命令 explain() 返回空 facts → 可用性误报 true；facts label 英文违背中文优先契约。
10. 三套帮助 authority 并存（HelpCenterDialog registry / HelpViewerDialog USER_GUIDE / SicnuDialogHelp 代码表），ID 体系不统一。
11. WorkbenchGuidance 组件无消费方（8 条 workbench guidance 数据不可达）。
12. 可访问性：禁用原因仅走 tooltip（325 setToolTip vs 3 setAccessibleDescription），屏幕阅读器不可达；⛔/⚠ 标记与 facts/扁平 reason 双通道不一致。

## 构建环境事实

- preset `dev-default`（Debug、ENABLE_TESTS=ON），binaryDir=`${sourceDir}/build-dev`，Unix Makefiles，C++20。
- FetchContent 依赖 catch2/pybind11 需网络；本环境 github HTTPS 克隆间歇 TLS EOF，已从主仓库 `build-dev/_deps`（pybind11@a2e59f0e、catch2）复制缓存解决。
- 资源：16 核 / 62GB RAM / ccache 可用（冷缓存）。硬约束 build `-j2`、test `-j1`、`QT_QPA_PLATFORM=offscreen`。
