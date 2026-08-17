# Task Plan: exp-rs UI / Rendering / Data / Interaction Audit

## Goal
对 WindWang2/exp-rs @ BASE_SHA `19843d1b6910c9207c7e5c97863a873db679368e` (origin/master) 进行生产质量深度审查：
1. 窗体/窗口/Dock/Dialog/Widget 生命周期与状态管理
2. 遥感影像/栅格/矢量显示、绘制、渲染、刷新与交互性能
3. 数据管理：打开、加载、缓存、生命周期、项目状态、元数据、派生数据、路径一致性
4. UI/UX/交互：工作流、状态反馈、错误反馈、工具状态、大数据可用性

产出：验证过的 finding → 去重 → GitHub Issues（英文）。master 只读。

## 硬约束
- 禁止修改 master 工作树 tracked 文件（用户有未提交修改，绝不动）
- 审计只读基于 `.scratch/audit-worktree`（detached @ BASE_SHA）
- 修复实验用一次性 worktree，不 push、不 PR
- Issue 门槛：confidence ≥ 0.85 + 确定性证据（复现/测试/sanitizer/benchmark/API contract/可稳定触发状态错误）
- 高 precision 优先：宁缺毋滥；目标是 6~15 个高质量 Issue

## 已知背景（去重基线）
- 仓库已有 #169–#241 共 70+ 个前轮审计 Issue（含 #181 swipe 不渲染、#182 波段组合未填充、#183 拉伸预设映射偏移、#184 批处理同步冻结、#185 选区工具 O(P²)、#216 渲染器无对比度增强、#217/#212/#218 GUI 线程同步重活、#220/#236/#238 生命周期/UAF 杂项、#237 Python 控制台线程等）
- 本轮所有 finding 必须先对照该清单去重，按 root cause 判断

## Phases

### Phase 1: Baseline & inventory [in_progress]
- [x] git fetch, BASE_SHA 冻结 = 19843d1b6910c9207c7e5c97863a873db679368e
- [x] gh auth 确认（WindWang2, full repo scope）
- [x] 既有 issues 全量拉取（#1–#241）
- [ ] 确认只读审计 worktree 可用性 + 构建环境探测
- [ ] 顶层目录结构 inventory（src/app、src/core、canvas、dialogs、docks…）

### Phase 2: Architecture / call-chain mapping [pending]
- [ ] main → MainWindow → menus/toolbars/docks/dialogs 结构图
- [ ] 渲染 pipeline 图（dataset→read→stretch→QImage→canvas）
- [ ] 数据管理图（layer tree→dataset manager→project state→derived）
- [ ] 产出 code_map.md / ui_workflows.md / rendering_map.md / data_lifecycle.md

### Phase 3: 并行子 Agent 深审 [pending]
- Agent A 窗体与 Qt 生命周期（ownership、重复创建、close/reopen、signal 累积）
- Agent B 渲染正确性（stretch/dtype/nodata/RGB/分块边界/极端尺寸）
- Agent C 渲染性能（全图重读、viewport 感知、overview、刷新风暴、QImage 拷贝）
- Agent D 数据管理（dataset ownership、路径、缓存 key、layer tree index、project 往返、reload/移除后状态）
- Agent E UI 状态与工作流（QAction 状态、工具模式、selection 同步、Cancel 语义、进度）
- Agent F 线程/异步渲染（stale render、generation id、cancellation、worker 访问已销毁 UI）
- 每个 Agent 输出结构化 finding（file:line、证据、可达性、复现条件、severity、confidence）

### Phase 4: Lead 复核 + 验证 [pending]
- [ ] 逐条读代码复核静态证据与可达性
- [ ] 构建/运行可行时做复现（测试、instrumentation、benchmark）
- [ ] 不满足门槛 → LOW_CONFIDENCE/FALSE_POSITIVE 归档

### Phase 5: 对抗性复核 [pending]
- [ ] 独立 Agent challenge 每条候选（10 问清单）

### Phase 6: 去重与分类 [pending]
- [ ] 对照 #169–#241 按 root cause 去重
- [ ] severity/type/area 定稿

### Phase 7: Issue 提交 [pending]
- [ ] 英文、模板格式、串行提交、记录编号

### Phase 8: 最终完整性验证 + 报告 [pending]
- [ ] git status 确认 master 干净
- [ ] worktree/branch 清理
- [ ] 最终报告

## Errors encountered
| 错误 | 尝试次数 | 解决方案 |
|------|---------|---------|
| (none) | | |
