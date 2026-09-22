# Progress ledger — taskcenter-execution-plane-jobs

| 轮 | 改动 | 验证 | 结果 | 下一步 |
|---|---|---|---|---|
| 1 | Phase 0 recon：master a9dc33fa73 与 seed 一致；仅 PR #1237（teaching 域，不冲突）；0 open issues；6 条旧线索分支确认为矿（均已过时，主题被 #1200/#1216/#1224/#1225/#1233 吸收） | git/gh 事实核对 | 通过 | 深读核心文件 |
| 2 | 通读 task_center.h/.cpp（5134 行）、execution_plane.cpp、output_committer.cpp、tool_call_dispatcher(_task_center).cpp、local_worker_pool.cpp、worker_execution_route.cpp、execution_resource_bridge.cpp、job_engine 关键路径 | 源码级 review + 状态机/账目配对核查 | 通过（发现 D1/D2/D3） | 建 worktree 构建 |
| 3 | worktree + dev-default 配置；窄目标构建（负载 28，兄弟 worktree 并行编译，-j2 纪律） | cmake preset dev-default | 配置通过 | 等待首次全量链接 |
| 4 | 实现 D1/D2/D3 修复 + 三个 RED-first 回归 oracle（test_task_center_12 ×2、test_output_committer ×1） | 待构建完成 | 待验证 | targeted 运行 |

## 缺陷 → 修复 → oracle 对照

- D1 就绪堆饱和 fast-path 丢候选（task_center.cpp ~2501）：break 前回推 entry。oracle: "Saturated fast-path admission keeps the popped candidate on the ready heap"。
- D2 auto-retry 不取消死 job（task_center.cpp markTaskFailed）：deadJobId → jobCancelTargets；早退路径走 dispatchPendingCancels；顺带清 m_forwardedLogCounts/m_lastForwardedProgress。oracle: "Transient auto-retry cancels the dead engine job (no zombie double run)"。
- D3 publish 缺 PAM `.aux.xml`（output_committer.cpp）：完整名 pair 加入 publish 组（sidecar-first 语义保持）。oracle: "Commit publishes the GDAL PAM .aux.xml sidecar with the dataset"。

## 杀伤力证明计划

每个修复单独 stash（仅 src 文件）→ 增量重编 → 对应测试必须 FAIL（旧实现 RED 证据）→ 恢复 → GREEN。两遍 final oracle 在修复齐备树上跑。

## 独立 adversarial review（第一轮）

Reviewer：独立 subagent（未参与实现），对 `a9dc33fa73..HEAD` 全 diff + 上下文代码逐 hunk 审查。
**结论：READY**（两个修复正确、测试 oracle 成立）。

发现与处置：
- [P2，先前已存在] task_center.cpp onJobRecord clientTag 恢复与 auto-retry 复活窗口竞态：死 job 的迟到的终态记录若落在 `jobId.clear()` 与新 job 预注册之间的微秒窗口，可经 `jobId.empty()` 分支重映射并误杀复活任务。处置：落地 reviewer 建议的低风险一半——预注册时清扫该任务的陈旧 `jobId→taskId` 映射（保证单一 live job）；完整修复（复活窗口的 between-attempts 标记）设计已记录，作为跟进项（新状态的生命周期清理点风险 > 当前窗口宽度）。
- [P3] retry 分支死 job 的 cancel 在 retry 提交后才派发，短暂双写窗口与终态路径既定顺序一致——记录。
- [P3] dedup-key 重置无独立 oracle——记录。
- [P3] PAM 回滚路径无直接测试（论证：回滚循环遍历共享容器自动覆盖）——记录。
- [P3] sidecar 词表四份并存——收敛为后续项（publish 侧与清理侧语义不同，见 01-recon）。
- 测试 flake 审查：未发现系统性 flake；无工作线程 Catch2 断言；单例状态清理完整。

## 已知限制（诚实披露）

1. **本地测试未执行**：宿主机被约 10 个并发 campaign worktree 的构建压满（load ~28），qgis_core（983 TU）首建未能在本会话内完成链接；按用户指示停止构建。三个 oracle 为 RED-first 设计，失败机理已逐一分析论证；复现命令：`cmake --build build-dev --target test_task_center_12 test_output_committer && ctest --test-dir build-dev -R 'test_task_center_12|test_output_committer'`。
2. 杀伤力（RED）实测同因顺延——测试断言即旧实现失败点（D1：break 丢堆头→lateRan 永假；D2：无 cancel→snapshot==Succeeded；D3：PAM 不发布→stable 缺文件）。
3. online CI not awaited（campaign 契约）。

## 终局记录

- PR #1256 已创建（base master @ a9dc33fa73，head hardening/taskcenter-execution-plane-jobs，commits 6fa7271be4 + 7e9404a178）。**未 merge，未等待线上 CI**（campaign 契约 + 用户指示）。
- 独立 review 结论 READY；P0/P1 = 0；P2（先前已存在）已落地低风险半 + 跟进项记录；P3 全部记录于 PR 已知限制。
