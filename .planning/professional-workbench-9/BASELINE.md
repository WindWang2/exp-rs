# BASELINE — 2026-09-12, re-established on latest origin/master

## 仓库状态

- 最新 `origin/master`: `f316dfdbb4` "fix(issues): resolve all 30 P1/P2 issues (#853-#882)"
- 本地 master 与 origin/master 同步；新 worktree `../exp-rs-professional-workbench-9`
  建立于该 SHA，分支 `feat/professional-workbench-9`。
- 前序方向已全部合入：#819–#847（6.0/7.0/8.0 全系列）+ #848-#852 P0 修复
  (`8f6293bceb`) + #853-#882 P1/P2 修复 (`f316dfdbb4`)。

## Open PRs（并行 9.0 波次，均不触本方向核心所有权）

| PR | branch | 主题 | 与本方向重叠 |
|----|--------|------|--------------|
| #883 | feat/scientific-algorithms-9 | raster semantics remediation + verification corpus | 无（core algorithms） |
| #884 | feat/model-runtime-multimodal-9 | CUDA lane / NVML / per-feed preprocessing | 无（models） |
| #885 | feat/spatial-scientist-harness-9 | Pi harness typed actions | 无（agent） |
| #886 | feat/scientific-mlops-9 | data versions / replay / promotion | 无（data） |
| — | feat/execution-concurrency-lifecycle-9 | 本地 worktree，未开 PR | execution seam，观察 |
| — | feat/geospatial-data-fabric-9 | 本地 worktree，未开 PR | I/O seam，观察 |

## Open issues

**0 个**。#848-#882 已被 `8f6293bceb` + `f316dfdbb4` 关闭 → 详见 ISSUE_TRIAGE.md
（五个历史风险 issue 在最新 master 上逐一重新复验，不是照单全收）。

## Remote branches

- `*-5/*-6/*-7/*-8` 全部是已合并 PR 的历史残留，不含有价值未合提交。
- 活跃 9.0 分支即上表 4 个 open PR + 2 个本地 worktree 分支。
- 本方向不复用任何历史分支。

## Workbench 8.0（PR #845）已交付（本方向的起点）

1. SchemaForm 4.0：递归嵌套、对象数组、注入式 `SchemaEnumProvider`（自由文本降级）、
   有界 RsScanPool 上的 async `x-ui-check`、可访问名。
2. AssetPreviewService：有界异步 preview（raster 重采样缩略图 / vector painter job），
   LRU 64 entries / 32 MiB，supersede/cancel/dead-receiver drops。
3. Data Manager catalog scaling：`AssetCatalogIndex` 增量过滤、lazy children、
   20 000 行有界渲染 + 真实 sentinel。
4. Context facts 8.0：`hasInFlightTask` / `hasBrokenLayer` / `suggestedNextAction`。

## 8.0 已声明的 follow-up（→ 本方向 milestone 映射）

| 8.0 limitation | 去向 |
|---|---|
| 无生产 host 装配 `SchemaEnumProvider`，所有 `x-ui-enum-source` 降级自由文本 | **M6** |
| oneOf/variants 拒绝（无 schema producer） | M6（条件性，维持拒绝除非 producer 出现） |
| catalog filter 纯客户端；store 侧 pushdown 需 DataManager 查询词汇 | **M7**（经 Track 3 seam） |
| preview mtime/size 缓存身份粒度 | M7 记录边界 |

## 本方向 baseline 新发现（见 ISSUE_TRIAGE.md 详证）

- **F1（M2）**：`main_window_menus.cpp` 用裸 `addAction(..., QKeySequence::New/Open/Save)`
  创建 project 菜单 action，与 CommandRegistry `project.new/open/save` 登记的同一
  canonical shortcut 构成**双权威**：registry 定义声称拥有快捷键，实际 shortcut 持有者
  是菜单裸 action；任一表面调用 `action(id, true)` 即触发 Qt ambiguous-shortcut。
- **F2（M2/#882 残留变体）**：`pipeline_editor_dock.cpp` 工具栏 tooltip 声称
  Ctrl+N/O/S 打开/新建/保存**工作流**，但这些 action 未绑定任何快捷键——真实
  Ctrl+N/O/S 属于 project.* 命令。tooltip 撒谎（#882 同类 drift，未修复干净）。
- **F3（M0/P2）**：`histogram_widget.cpp` worker `GDALOpen` 失败静默 return，无错误
  上抛（无 stuck-state，因无 busy 指示，但违背 empty/error-state 一致性）。
- #849/#857/#859/#861 主体修复确认有效（见 ISSUE_TRIAGE.md 逐项证据）。

## 构建与测试基线

- 预设 `ci-fast`（Release, ENABLE_TESTS=ON, Ninja）；测试 offscreen Qt + Catch2 + CTest。
- 资源纪律：构建 `-j2`（至多 `-j4`）、测试 `-j1`；重型测试不与其他 worktree 并发。
