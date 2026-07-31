# Findings — Code Review #109–#112 与修复

## 审查范围

- 基准:`git diff origin/master...HEAD`,4 个 commit(#109 `499baafe8a`、#110 `9dd812bc47`、#111 `6d3c0e85c9`、#112 `1687480ab9`),17 文件,+309/−237。
- Spec 来源:GitHub issues #109–#112(`gh issue view`)。
- 标准来源:根目录 `CLAUDE.md` "Coding Style & Standards"(仓库无独立 CODING_STANDARDS/CONTRIBUTING)。

## Standards 轴发现(0 硬违规,4 项 judgement call)

| # | Smell | 位置 | 处置 |
|---|-------|------|------|
| S1 | Repeated Switches | `workspace_snapshot.cpp` 两处相同 4 分支 switch | ✅ 提取 `assetKindToString()` |
| S2 | Speculative Generality | `TaskCenter::waitForPipeline()` 零调用者 | ✅ `rs_pipeline_runner.cpp` 迁移后获得真实调用者 |
| S3 | Duplicated Code | 超时字面量 "Tool call timed out in TaskCenter" 两处重复 | ✅ 共享常量 `kToolCallTimeoutMessage`(task_center.h) |
| S4 | Mysterious default | `DataAssetInfo::kind` 默认误标为 `Raster` | ✅ 改 `std::optional`,未设置序列化 `"Unknown"` |

## Spec 轴发现(1 缺失、2 scope creep、2 实现偏差)

| # | 类型 | 描述 | 处置 |
|---|------|------|------|
| P1 | 缺失(#110) | `rs_pipeline_runner.cpp` 轮询循环未迁移,验收标准 "call sites updated" 未达成 | ✅ 迁移到 `waitForPipeline()` |
| P2 | scope creep(#109) | `createHelper` 横幅 print 被删 | ✅ 已还原(`54412c0636`) |
| P3 | scope creep(#110) | 超时新增 `status="error"` + 空 errorMessage 推断 | ✅ 已还原:终态/超时分支分离(`54412c0636`) |
| P4 | 实现偏差(#112) | 默认值从 `"Unknown"` 翻转为 `Raster`,JSON 输出并非"unchanged" | ✅ 同 S4 |
| P5 | 实现偏差(#112) | stringify 重复(同 S1) | ✅ 同 S1 |

## 验证记录

- 核心修复后(6 套件 185 断言):test_workspace_snapshot 38、test_task_center 73、test_pipeline_runner 31、test_algorithm_engine 25、test_agent_canvas_sync 5、test_agent_workflow_executor 13 — 全绿。
- scope creep 还原后(4 套件 123 断言):test_python_engine 36、test_python_plugin_manager 69、test_agent_workflow_executor 13、test_agent_canvas_sync 5 — 全绿。

## 关键事实(供后续会话)

- 本地 `master` = `54412c0636`;`ab4fa3969e`(核心修复)已推送到 `origin/master`,`54412c0636` 尚未推送。推送前需征得用户同意,禁止 force push。
- `waitForTask`/`waitForPipeline` 契约:超时返回非终态 status + 空 errorMessage;调用点应用 `isTerminalStatus()` 区分终态失败与超时,不要靠 errorMessage 空值推断。
- `AssetKind` 全域枚举(`src/data/asset_types.h`)被 6+ 处 switch 使用且大多无 default 分支;新增枚举值需评估编译告警面。
- 超时语义已按用户决定还原为 #110 前契约:executor 超时响应**不带** `status` 字段,下游如依赖 `status=="error"` 判超时需注意。

---
*2026-07-29 前次任务发现(CollectionImportService Deepening / ADR 0018)已被本文件替换;完整记录见 git 历史。*
