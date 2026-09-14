# GOAL — professional-workbench-visual-cartography-10 · 统一专业工作台 / Visual Analytics / Cartography 10.0

/goal  target-agent=zcode  budget=300000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Track branch:** `zcode/professional-workbench-visual-cartography-10` (worktree
> `../exp-rs-professional-workbench-visual-cartography-10`, off `origin/master` @ `7d78059d1a6d316d606656759a506d17bc5e3b55`)
> **Mode:** unattended long-running epic. Local build/test evidence only — never block on,
> trigger, or cite online CI.
> **Write scope:** `src/app/**`（workbench / visual analytics / command/context projection /
> cartography UI-workflow bridge）、`src/workflow/**`（仅窄缝：可视化编辑所需类型投影）、
> `tests/**`（新增/扩展本 track 套件）、`docs/**`（本 track 文档）、
> `.planning/professional-workbench-visual-cartography-10/**`、`.gitignore`（本 track 白名单行）、
> `src/operators/` 仅限**append-only** 注册 cartography operator family（C-1 统一 node/tool contract，
> 单独 integration commit）。
> **Read-only:** `src/core/**` `src/gui/**`（QGIS vendored）、`src/processing/**`（TaskCenter/JobEngine 权威，
> execution-concurrency track 域）、`src/geospatial/**`（I/O 权威）、`src/runtime/**`、`src/dataset/**`
> `src/experiment/**`（store 权威，仅调用）、`src/agent/**`（仅窄集成：context projection seam）。

## Mission

工作台 9.0（PR #890）已交付 WorkbenchStateModel、CommandRegistry 单一权威、SchemaFormHost 5.0、
分页 catalog 与插件 declarative UI（`docs/ui-architecture.md` Part V）；cartography-platform-9（PR #889）
交付了 Qt-free 的 MapSpec compose/preflight/repair/export 引擎与 22 个 `cartography:*` 代理工具
（`src/agent/cartography/cartography_tools.cpp:1968-1987`）。但两条链在 GUI 里互不相通：
`grep -rln MapSpec src/app/` 为空 —— 制图能力对桌面用户不可达，分析结果无法进排版导出
（`ISSUES.md:45` C-1）；`SelectionContext`（`src/app/workbench/selection_context.h:39-70`）
只有 layer/asset/result 三类事实，Experiment/Model/Workflow 选择与稳定对象身份缺失；
可视化组件（histogram/spectral/cross-section/comparison）各自绑定 QgsRasterLayer、自扫像素
（`src/app/widgets/histogram_widget.h:45`），无 typed 数据源契约、无 linked brushing。

做完之后：Data/Layer/Result/Experiment/Model/Workflow 在统一 selection identity 下贯通
（inspector/命令可用性/agent context 同源投影）；MapSpec compose/preflight/repair/export
经 workflow node/tool contract 可被 GUI、agent 与 headless pipeline 复用（C-1 关闭）；
visual analytics 成为平台组件（typed source、有界采样、取消、空/错/载状态、主题 token、导出）；
多视图联动（extent/cursor/visibility）建立在 QgisDisplayManager 的 view 权威上。

## Operating envelope (non-negotiable)

- **Autonomy**: fully unattended. No clarifying questions, no option menus. Take the
  default in Autonomy defaults; record every taken decision in
  `.planning/professional-workbench-visual-cartography-10/DECISIONS.md`.
- **Precedence**: this GOAL overrides `.agents/AGENTS.md` / `CLAUDE.md` on conflict.
  AGENTS.md §1's "surface options / clarify" duty is discharged by writing options and
  the taken default into DECISIONS.md.
- **Agent**: `zcode`. **Token budget: 300,000,000**（allocation below）。When a phase
  exceeds 1.5× its allocation: append a budget line (phase, clock time, commands run,
  files touched) to EVIDENCE.md and continue — never stall silently, never ask.
- **Subagents: at most 2**, both read-only（Subagent A：Phase 7 架构/科学语义/历史去重审查；
  Subagent B：Phase 7 测试可信度/性能与并发/移植审查）。
  Main agent owns 100% of implementation and judgment. Subagents spawn no further subagents.
  A subagent that fails or returns nothing usable: the main agent performs that review
  inline, records the failure in EVIDENCE.md, and does not spend an extra slot.
- **No CI**: never wait on, trigger, or cite GitHub Actions. Local evidence only →
  `.planning/professional-workbench-visual-cartography-10/EVIDENCE.md`. Every capability
  claim maps to a local command + exit code, or is explicitly marked not-executed.
- **Branching**: `master` is read-only. Create worktree + branch before the first edit.
- **Build entry**: `cmake --preset dev-default`（CMakePresets.json；build dir `build-dev/`）；
  configure exit code 记入 EVIDENCE.md。`build.cmd` / `configure_*.cmd` 不使用（defect D-028）。
- **Build resources (hard)**: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`;
  Ninja `-j2`, drop to `-j1` when RSS > 70% or load > 1.5× cores; `-j$(nproc)` is
  forbidden. Measure RSS with `ps`, load via `uptime`. Log CPU/RSS every 60 s during builds.
- **Tests**: `QT_QPA_PLATFORM=offscreen`; targeted `ctest -R <family> -j1` before any
  broad run.
- **Exit**: PR created, not merged; worktree retained until merge, then removed.

## Skills (load proactively)

| Skill | Use it for | Phase |
| --- | --- | --- |
| `.agents/skills/resolving-merge-conflicts/SKILL.md` | rebase 冲突时的流程参考 | 8 |
| `.agents/skills/code-review/SKILL.md` | Phase 7 自审清单参考 | 7 |
| `.agents/skills/qt-cpp-review/SKILL.md` | Qt/C++ 专项 review 参考 | 7 |

（三处均已 `ls .agents/skills/<name>/` 验证存在。）

## Autonomy defaults (do not ask — apply these)

1. **格式/来源**：对象身份沿用各权威 store 的既有 id（AssetId / DisplayLayerId /
   governance entity id / experiment run id / dataset id / ModelCatalog name /
   workflow pipelineId）；MapSpec 输入用文件路径 + 占位符语法
   （`src/workflow/placeholder_grammar.h`）解析；UI 文案中文，与现有一致。
2. **失败项处置**：单个测试失败 → 修复或如实记录为已知失败，不跳过不掩盖；
   不支持的 capability（如无 QGIS 的 headless export）→ 类型化拒绝（诚实错误码）。
3. **命名/编号**：新面板 `src/app/visualanalytics/`、`src/app/cartography/`；
   命令 id 前缀 `cartography.`、`view.`、`va.`（registry 唯一性由注册期保证）；
   冲突时改用更长前缀并在 DECISIONS.md 记录。
4. **资源与超时**：单命令 ≤ 10 min（configure/build 阶段 ≤ 60 min）；超时降 `-j1` 重试一次，
   再超时记录后继续其余工作。
5. **对外动作**：`git fetch origin` 允许（只读）；`git push` 本 track 分支 + `gh pr create`
   允许；其余（issue 评论、release、merge）禁止。
6. **范围外发现**：记入 EVIDENCE.md `OUT_OF_SCOPE` 节；P0 级另在 PR_BODY.md 顶部以
   `P0 (out of scope)` 标注；不在本 track 修复。
7. **依赖新增**：禁止新增第三方依赖；绘图用 QPainter/QSvg 自绘（沿用现有 widgets 模式）。

## Current state & evidence (verified)

| Fact | Evidence |
| --- | --- |
| MapSpec 在 `src/app/` 零引用（GUI 不可达） | `grep -rln MapSpec src/app/` → 空 |
| `cartography:*` 仅注册为 agent 工具 | `src/agent/cartography/cartography_tools.cpp:1968-1987` |
| C-1：compose/preflight/export 不是管道算子 | `ISSUES.md:45-49` |
| SelectionContext 仅 layer/asset/result 事实 | `src/app/workbench/selection_context.h:39-70` |
| WorkflowIR = WorkflowDefinition/StepDef | `src/workflow/workflow_types.h:33-53` |
| workflow 步骤经 RSOperatorRegistry+TaskCenter 执行 | `src/app/shell/workflow_session_controller.cpp:162-300` |
| QgisDisplayManager 已支持 N 视图 | `src/app/display/qgis_display_manager.h:148-238` |
| 图表 widget 直接绑定 QgsRasterLayer | `src/app/widgets/histogram_widget.h:45`、`spectral_profile_widget.h:41` |
| agent→画布单向已通（plan→WorkflowDefinition→canvas） | `tests/test_agent_canvas_sync.cpp:30-40` |
| MCP server 无 workbench context 投影 | `grep -n "workbench\\|selection" src/agent/mcp_server.h` → 空 |
| MapSpecCompiler 已桥接 QgsPrintLayout | `src/agent/mapspec/mapspec_compiler.h:25-40` |
| compose solver 与 preflight Qt-free | `src/agent/cartography/composition.h:30`（仅 json）、`quality.h:22` |

## Work packages

| ID | Package | Key deliverables |
| --- | --- | --- |
| A | 统一对象身份与 selection/context | `object_identity`（typed ObjectRef）、SelectionContext 扩展（experiment/dataset/model/workflow）、ContextRules 扩展、agent context 投影 seam、测试 |
| B | Cartography bridge（C-1） | cartography operator family（append-only 注册）、制图 workbench dock（compose/preflight/repair/export/preview）、结果→MapSpec 输入、命令注册、测试 |
| C | Visual Analytics 平台 | `src/app/visualanalytics/`（typed sources + chart hosts + 有界采样/取消/状态机 + 主题 token + 导出）、linked brushing hub、结果面板消费、测试 |
| D | Workflow 可视化编辑 2.0 | 节点 IO 类型徽标、参数 inspector（SchemaForm 复用）、preflight 错误/修复投影到节点、测试 |
| E | 多视图联动 | link controller（extent/cursor/visibility，N 视图）、视图管理命令、安全契约测试 |
| F | Processing UX 收敛 | 算子目录搜索/最近/收藏、capability 过滤、live availability，测试 |
| G | Review/验证/PR | 双 subagent 对抗 review、P0/P1 清零、规模/边界证据、最终 HEAD 验证、PR |

## Execution order & token budget (300,000,000 total)

| Phase | Content | Budget (M tokens) |
| --- | --- | --- |
| 0 | 基线审计 + 能力矩阵 + GOAL 落盘 | 18 |
| 1 | WP-A 统一对象身份与 selection/context | 48 |
| 2 | WP-B Cartography bridge（最大包） | 54 |
| 3 | WP-C Visual Analytics 平台 | 46 |
| 4 | WP-D + WP-E（编辑器 2.0 + 多视图） | 38 |
| 5 | WP-F + 测试/文档/a11y 补全 | 34 |
| 6 | 交叉复核（≤2 只读子代理） | 24 |
| 7 | 复核发现修复 | 20 |
| 8 | Rebase + docs 同步 + PR | 18 |

（总和 = 300。计量方式：每 Phase 结束记 `时间戳/工具调用次数/触及文件数` 入 EVIDENCE.md。）

## Required planning files

`.planning/professional-workbench-visual-cartography-10/`：`GOAL.md`（本文存档）· `PLAN.md` ·
`BASELINE.md` · `DECISIONS.md` · `EVIDENCE.md` · `REVIEW_LOG.md` · `PR_BODY.md` ·
`MILESTONES.md` · `TEST_MATRIX.md` · `CAPABILITY_MATRIX.md` · `OWNERSHIP.md` ·
`ARCHITECTURE.md` · `PROGRESS.md` · `PERFORMANCE.md`。

## Completion gate

1. 统一 selection：`ctest -R test_selection_context -j1` 全绿且新增身份事实有断言。
2. Cartography bridge：`ctest -R test_cartography_workflow_bridge -j1` 全绿；GUI dock 命令
   经 CommandRegistry（`test_command_registry` 回归全绿）。
3. VA 平台：`ctest -R test_visual_analytics -j1` 全绿（typed source、有界采样、状态机、导出）。
4. 编辑器 2.0：`ctest -R test_pipeline -j1` 全绿（IO 徽标 + preflight 投影）。
5. 多视图：`ctest -R test_view_link -j1` 全绿；`test_qgis_display_manager` 回归全绿。
6. 对抗 review：REVIEW_LOG.md 中 P0 = 0、P1 = 0（逐条 disposition + evidence）。
7. 最终 HEAD 本地验证：build-dev 目标编译通过 + TEST_MATRIX.md 列出的套件全绿；
   `git diff --check` 干净；冲突标记扫描为空。
8. PR 已创建（`gh pr create`），未 merge。

## PR runbook (execute verbatim)

1. `git worktree add ../exp-rs-professional-workbench-visual-cartography-10 -b
   zcode/professional-workbench-visual-cartography-10 origin/master`（已完成）；此后全部工作
   （含本 GOAL.md 的落盘）只发生在 worktree 内。
2. 向 `.gitignore` 追加本 track 白名单（三行模式，照 `.planning/lab-spec-data-driven/` 条目），
   执行 `git check-ignore -v .planning/professional-workbench-visual-cartography-10/GOAL.md`，
   确认无输出（exit 1）后，把 `.gitignore` 与 `.planning/<slug>/` 规划文件一并作为首次 commit。
3. 每个 Phase 完成后 commit 一次；把 `git status --porcelain` 的完整输出粘进
   EVIDENCE.md 对应 Phase 小节。
4. 每个 Phase commit 后执行 `git fetch origin && git rebase origin/master`；冲突时
   加载 `.agents/skills/resolving-merge-conflicts/SKILL.md`，其验证步骤按本 GOAL 的 Tests 行执行
   （targeted `ctest -R <family> -j1`）。
5. 本地验证按 Build resources 硬约束执行；输出进 EVIDENCE.md。
6. 最后提交前执行存在性断言（goal-template「存在性断言」一节），结果粘进 EVIDENCE.md。
7. `git push -u origin zcode/professional-workbench-visual-cartography-10`。失败时：stderr 原文
   记入 EVIDENCE.md；若错误含 hook/BLOCKED 字样，原样重试一次；仍失败则跳到第 10 步。
   force push 在任何情况下禁止。
8. `gh pr create --base master --head zcode/professional-workbench-visual-cartography-10
   --title "<type>(<scope>): <summary>" --body-file .planning/<slug>/PR_BODY.md`。
   Do not merge. Do not wait for checks. Report the PR URL and stop.
9. 收到 reviewer 修改请求：视为同一 track 的续跑——逐条回复、修改、commit、push，
   重跑第 6 步断言；仍 do not merge。
10. 任何一步无法完成：在 EVIDENCE.md 写收尾报告（已完成工作包、失败步骤原文、退出码），
    向用户报告后停止。
