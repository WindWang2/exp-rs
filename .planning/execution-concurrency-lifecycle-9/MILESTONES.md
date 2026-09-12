# MILESTONES — 完成状态追踪

| 里程碑 | 状态 | 交付物 |
|---|---|---|
| M0 P0/P1 burn-down | **代码完成，待编译验证** | #860 通知队列+drain；#876 ghost 终态闭合；#862 transient 准入 + wait 拒绝；#851/#852 复验（基点已修） |
| M1 Structured Task Hierarchy | **代码完成，待编译验证** | ownerTaskId + m_ownedChildren + join 规则 + 双边取消传播 + currentJobId |
| M2 Thread-Affinity & Snapshot | **代码完成，待编译验证** | #852 契约收尾（移除过时 reader 警告）+ generation 单调测试 |
| M3 Unified Cancellation/Pause/Resume | **代码完成，待编译验证** | TaskCancelReason 贯通 + terminal 单调守卫 + pause/resume typed 测试 |
| M4 Lifecycle-Safe Eventing | **代码完成，待编译验证** | 全 emit 点审计（coordinator 修复、TaskCenter/JobEngine 已合规）+ seq 进 trace |
| M5 Resource Admission 9.0 | **代码完成，待编译验证** | admissionSnapshot transient 字段 + terminal RSS 观测 trace |
| M6 Durable Checkpoint 4.0 | **复验完成 + 测试补强** | 8.0 已交付原子写/身份门/损坏拒绝；新增 corrupt typed refusal 测试 |
| M7 Observability | **代码完成，待编译验证** | TaskCenter::explainDump + coordinator explainRun/explainDump |
| M8 Scale/Soak/Fault | **测试就绪，待运行** | 300 深链 / cancel storm / 100k 逻辑规模（门控）/ 既有 ep7 10k 回归 |
| Adversarial review | 待办 | ≤2 只读 subagents |
| PR | 待办 | push + PR 描述（本地证据、未运行项、已知限制） |
