# CURRENT_ARCHITECTURE — 帮助/诊断/i18n authority 现状图（master @ a5b11b7f10）

## Authority 分层（既有，本 track 不换真值）

```
┌─ 派生事实层（代码内，运行期真值）──────────────────────────────┐
│ CommandRegistry (src/app/command/command_registry.*)           │
│   CommandDefinition{id,title,purpose,availability,explain}     │
│ RSOperatorRegistry (src/operators) → OperatorCatalogSource     │
│ ContextRules (src/app/workbench/selection_context.*) 纯函数    │
│ harness_error.h k* 码 + RetryClass；rs_operator_error.h ErrorCode│
└──────────────┬───────────────────────────────────────────────┘
               │ provider 校验（孤儿知识=错误；schema 外知识=错误）
┌──────────────▼───────────────────────────────────────────────┐
│ sicnu_help (src/help/，Qt Core+data+jsoncpp only)              │
│  HelpId(kind.domain.name)  HelpRegistry(globalHelpRegistry)    │
│  HelpContentStore ← qrc :/help ← data/help/*.json（知识附加层）│
│  composeHelpSystem: JSON+Terminology+Command+Operator 四源合并 │
│  HelpSearchIndex / HelpPresenter / HelpTopicText               │
│  HelpMarkdownWriter(5 页) / DiagnosticCatalog / AvailabilityFacts│
└──────────────┬───────────────────────────────────────────────┘
               │
┌──────────────▼───────────────────────────────────────────────┐
│ Surfaces: GUI(HelpSystemController/F1/HelpCenter/HelpViewer/   │
│  SicnuDialogHelp/schema_form_builder) · CLI(--operator-help/   │
│  --help-topic/--list-topics/--export-help-docs) · MCP(get_tool_help) │
└───────────────────────────────────────────────────────────────┘
```

## 关键不变量（既有 gate）

1. 知识→注册表孤儿 = 组合期错误（command_help_provider.cpp:87-91、operator_help_provider.cpp:203-210）。
2. diagnostics.json 条目 id 必须 == HelpId::diagnosticId(family,code) 派生（help_content_store.cpp:159-164）。
3. harness/operator 错误码 census：每码须有 curated 页（test_diagnostics_contract_9，白名单仅 TEACHING_REFUSAL）。
4. 命令 census：每注册命令须有知识页 + 反向孤儿（test_help_coverage 源码扫描 + test_command_contract_9 运行期双向）。
5. zh_CN.ts 零 unfinished、messages≥3000（test_i18n）。
6. HelpDescriptor 不存 schema 范围/默认值/快捷键/可用性事实（help_descriptor.h:4-10）——本 track 的 resolver 必须延续此解耦。

## 双扫描器漂移点（本 track 要收敛的 oracle 缺陷）

- test_help_coverage.cpp:75,94 源码扫描：前缀集合 (project|layer|map|workbench|rs|app)、idiom `\.id\s*=\s*…"\)`。
- contracts/command_ref_scanner.cpp:34-58（test_command_contract_9 用）：四种 idiom。
- 两套独立维护 → workflow.* 前缀漏扫、`" );` 变体漏扫。
