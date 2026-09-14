# Professional Workbench / Visual Analytics / Cartography 10.0

**Baseline**: `origin/master` @ `7d78059d1a`（PR #958 合并点）· worktree
`../exp-rs-professional-workbench-visual-cartography-10` · branch
`zcode/professional-workbench-visual-cartography-10`。

> **Local evidence only; no online CI dependency.** 未触发/未等待/未引用任何线上 CI。
> 全部验证为本地可复现：Release 构建（dev-default preset）+ offscreen 测试矩阵
> 26/26 套件全绿 + test_mapspec 601/602 断言，最终 HEAD 上执行（详见
> `.planning/professional-workbench-visual-cartography-10/TEST_MATRIX.md` / `EVIDENCE.md`）。

## Mission（现状 → 变化）

工作台 9.0 与 cartography-platform-9 分别交付了 shell 权威（StateModel/
CommandRegistry/SchemaForm 5.0）与 Qt-free 制图引擎（compose/preflight/repair/
export + 22 个 agent 工具），但 GUI 里互不相通：MapSpec 在 `src/app/` 零引用
（桌面用户不可达制图，ISSUES.md C-1）；SelectionContext 仅 layer/asset/result
三类事实；图表 widget 各自绑定图层自扫像素；MCP 无工作台上下文投影。

本 track 之后：六个对象域（Data/Layer/Result/Dataset/ExperimentRun/Model/
WorkflowRun）在统一类型化身份下贯通；cartography 经 workflow node/tool
contract 同一引擎四路可达（GUI/工作流/CLI/agent）；visual analytics 成为
平台组件；UI→agent 有了只读 context 投影。

## Architecture（As-Built，详见 ARCHITECTURE.md）

既有权威零重建：TaskCenter/JobEngine、QGIS、MapSpecCompiler、composition
solver、WorkspaceService/DataManager、SelectionContext/CommandRegistry/
WorkbenchStateModel、QgisDisplayManager、RsScanPool、workflow:preflight。

## Major deliverables

- **WP-A 统一对象身份**：`object_identity.{h,cpp}`（ObjectKind/WorkbenchObjectRef
  复用各 store 既有 id；primaryObject 确定性优先级规则）；SelectionContext 增
  dataset/experiment-run/model/workflow-run 事实与 push 式通知；ContextFacts/
  ContextRules 扩展；溯源目标解析收敛为单一 `resolveSelectionAssetTargets`
  （ProvenanceSection 改调共享 helper，行为零漂移）。
- **WP-B Cartography bridge（关闭 C-1）**：`cartography:*` 五个 RSOperator
  （compose/preflight/validate/repair/export）——与 agent 工具同一引擎、同 schema
  同行为（resolve→preflight 顺序、repair 有界循环与台账、export pages 回显、
  sha256 证据）；`initCartographyOperators()` 由 app/CLI main 调用（幂等）；
  `CartographyDock`（模板目录、经算子驱动全部动作、72dpi 有界预览、导出证据）；
  `workbench.cartography` + `cartography.*` 命令；cartography workflow preset
  （`${carto_import.output}` 占位符链：分析结果 → MapSpec → 导出）。
- **WP-C Visual Analytics 平台**：typed payloads（6 族，全部有界 + truncated
  诚实标注）；`VaDataSource`（RsScanPool + generation 取消 + QPointer 守卫
  marshal）；`VaChartWidget`（token 配色、绘制封顶、brushing 信号、CSV/JSON
  导出）；`VaWorkbenchPanel`（直方图/散点/波段均值曲线，缩略图采样诚实标注
  "抽样估计"，直方图→散点联动过滤）；`workbench.visualAnalytics` 命令。
- **WP-D Workflow 编辑器 2.0**：节点选择经 `selectStep` 驱动 TaskPanelHost 步骤
  表单（单一 schema-form 面）；编辑器"检查"投影共享 `workflow:preflight` 引擎
  到节点错误徽标 + 可修复提示。
- **WP-E 多视图联动**：`ViewLinkController`（Display Manager 视图上的 N 视图
  extent 联动：per-view 开关、16ms 节流、跨 CRS 变换、移除自动 detach）；
  类与测试交付，shell 装配记 follow-up。
- **WP-F rs: 算子目录**：`RsOperatorCatalogPanel`（registry + capability
  knowledge 侧车；搜索/模态过滤/最近(≤12)/收藏；QSettings
  `workbench/processing/*`）；打开 = 合成单步 workflow 定义 → `openTool` →
  TaskCenter（唯一执行者不变）。
- **WP-H Agent 共存**：`workbench:context` 只读 SpatialTool（MCP `workbench:`
  前缀路由；headless 不注册不可见；QPointer 守卫 provider）；写入路径零扩大。

## Compatibility

- 无 schema/协议破坏；registry 只增（`workbench.cartography`、`cartography.*`、
  `workbench.visualAnalytics`、`workbench.operatorCatalog`，全部无快捷键声明，
  shortcut 唯一性 gate 绿）；SelectionContext 仅追加字段/谓词（既有语义不动，
  wb5-9 套件全绿）；workflow 类型零改动；`mcp_server` 仅增前缀路由。
- 三处 master 基线修复（独立 commit、行为如实声明）：
  `test_shortcut_conflicts` GCC16 编译修复；`test_provenance_section` /
  `test_processing_history_model` / `test_selection_context` 的 i18n 期望漂移
  修复（PR #953 机械重写 tr() 源文未同步测试——这些套件在 master 上是红的，
  本 track 后全绿）。

## Tests

新增 6 套件：`test_object_identity`(5)、`test_agent_workbench_context`(4)、
`test_visual_analytics`(5)、`test_view_link`(3)、`test_processing_catalog_ux`(4)、
`test_cartography_operators_10`(5, test_mapspec harness 内)。回归矩阵 26/26
套件全绿（含 test_qgis_display_manager 343 断言、test_ui_task_center_contract
174 断言、test_mapspec 601/602）。执行记录逐套件见 TEST_MATRIX.md。

## Performance / Resource

全部新热路径有界：VA 采样 512²/256² 缩略图（OverviewPolicy::Nearest，单 reader
复用）、≤4096 散点；painter 封顶 4096 点/256 柱/4096 格；目录面板 400 行可见
上限 + 增量过滤；制图预览 72dpi/1100px；compose/preflight/repair 为 O(文档)
JSON 遍历。构建 -j2 / 测试 -j1，offscreen。

## Review findings

2 个只读对抗 reviewer（A 架构/语义，B 测试/性能/并发/移植）+ 主 agent 自审。
**P0 = 0；P1 = 4（全修）；P2 = 7（全修）；P3 = 16（修 6、接受 10 并逐条附
理由）**。逐条 finding → disposition → evidence 见 REVIEW_LOG.md。修复后
最终 HEAD 矩阵重跑全绿。

## Known limitations / follow-ups（诚实清单）

- PNG 渲染确定性用例在本机不稳定（master 同；字体环境）——OUT_OF_SCOPE，建议
  制图 track 固定测试字体。
- ViewLinkController 未在 shell 装配（无 view.* 命令）；cursor/visibility 同步
  未实现（ARCHITECTURE D10-6 As-Built 注明）。
- CartographyDock 动作为同步执行（有界文档；工作流路径本就异步）；后台化记
  follow-up。
- `availableCommands` 是注册词汇表而非可用性保证（按命令过滤需 registry 新 API）。
- catalog 打开路径跨 setup 顺序的守卫已在 handler 内；若未来引入多窗口重建，
  WorkbenchContextTool 需 replace-by-name 语义。
- Experiment lineage 的结构化 objectLinks 投影、进程级 VA selection hub、
  全 nodata 波段空态 marker：follow-up。

## Follow-ups

如上；另加：cartography 预设端到端 headless 用例（需 CLI QGIS host fixture）、
`view.*` 装配与命令族、`experiment./dataset./model./workflowrun.` 命令族注册。

## Commits

1. `docs(planning)` 基线与 GOAL（.gitignore 白名单）
2. `feat(workbench)` WP-A~F 平台主体
3. `fix(tests)` master 基线修复（shortcut GCC16 + 溯源/历史/选择 i18n 期望）
4. `fix(tests)` #953 期望同步第二批
5. `fix(workbench)` 对抗审查修复（本 commit 前）
