# Task Plan: Code Review #109–#112(origin/master...HEAD)与发现修复

## Goal
对本地 `master` 领先 `origin/master` 的 4 个 commit(#109–#112)执行双轴 code review(Standards + Spec),并修复全部审查发现。

## Current Phase
Phase 4: Delivery

## Phases

### Phase 1: 双轴并行审查
- [x] 固定基准点 `origin/master`(4 commit,#109–#112;#108 已在 origin 上)
- [x] 拉取 GitHub issue #109–#112 作为 spec 来源
- [x] Standards 子代理:CLAUDE.md 文档标准 + Fowler smell 基线 → 0 硬违规,4 项 judgement call
- [x] Spec 子代理:4 份 issue 验收标准对照 → 1 缺失、2 scope creep、2 实现偏差
- **Status:** complete

### Phase 2: 核心修复(commit `ab4fa3969e`)
- [x] `workspace_snapshot.cpp` 提取单一 `assetKindToString()`(消除重复 switch,未识别值回落 `"Unknown"`)
- [x] `DataAssetInfo::kind` 改为 `std::optional<data::AssetKind>`(默认值不再误标为 Raster,JSON 恢复 `"Unknown"`)
- [x] `rs_pipeline_runner.cpp` 轮询循环迁移到 `TaskCenter::waitForPipeline()`(#110 缺口 + 消除零调用者 API)
- [x] `task_center.h` 新增共享常量 `kToolCallTimeoutMessage`(消除两处重复字面量)
- [x] `test_workspace_snapshot.cpp` 新增 Unknown 默认值 SECTION
- **Status:** complete(由用户会话提交并推送为 `ab4fa3969e`)

### Phase 3: Scope Creep 还原(commit `54412c0636`)
- [x] 恢复 #109 `createHelper` 横幅 `print("SICNU helper loaded…")`
- [x] `agent_workflow_executor.cpp` 还原超时语义:仅终态失败设 `status="error"`;超时只设 `errorMessage`
- [x] `llm_streaming_client.cpp` 区分终态失败(原始 errorMessage)与超时(共享常量)
- **Status:** complete

### Phase 4: Verification & Delivery
- [x] 构建并运行 6 个测试套件全绿(见 progress.md)
- [x] 提交 scope creep 还原为 `54412c0636`
- [x] 同步规划文件
- **Status:** complete

## Key Questions
1. `AssetKind` 默认值如何修复而不波及全域枚举?
   *Resolution*: 不动 `src/data/asset_types.h` 的 `AssetKind`(6+ 处 switch 使用),在 agent 层用 `std::optional` 表达"未设置"。
2. runner 循环有进度上报副作用,如何迁移到 wait helper?
   *Resolution*: 外层循环保留进度上报,每轮调用 `waitForPipeline(pipelineId, kPipelinePollInterval)` 委托轮询/休眠机制。
3. 两处 scope creep 是否还原?
   *Resolution*: 用户确认还原。#110 超时不再补 `status="error"`(恢复 #110 前契约);#109 横幅 print 恢复。

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| `std::optional<AssetKind>` 而非新增 `Unknown` 枚举值 | 避免修改共享域枚举波及 data_manager_panel / qgis_display_manager 等 6+ 处 switch |
| `waitForPipeline` 小超时嵌套外层进度循环 | 保留 CLI 进度上报行为,同时消除手工 getPipelineInfo+sleep_for 轮询 |
| 共享 `kToolCallTimeoutMessage` 常量 | 两个 tool-call 调用点(executor std::string / llm QString)统一超时文案 |
| 超时与终态失败分支分离 | 消除"空 errorMessage ⇒ 超时"推断在残留错误信息下的误报边界 |

---
*上一任务(CollectionImportService Deepening,ADR 0018)已于 2026-07-29 全部交付,详见 git 历史与 progress.md。*
