# R28-T02 goal-loop 账本（工作副本；提交时并入仓库根 .goal-loop-ledger.md 顶部）

Oracle：R28-T02 任务包 §4 全部完成条件 + ACCEPTANCE.md G1-G10 全绿；PR 真实 URL 返回。

| 轮次 | WP | 假设 | 修改 | 命令 | 实际结果 | 下一步 |
|---|---|---|---|---|---|---|
| 1 | 准备 | 环境与基线可建立 | worktree+规划文档 | git fetch/worktree add | BASE=50cc3255c5；锁协议=flock 文件（30min 假死锁排查后确认） | configure 排队 |
| 2 | 准备 | configure 可行（ccache+launcher） | cmake -S/-B + ccache | 双层 resource_guard | CONFIGURE_OK；三 agent 锁轮转（T01→T03→我） | 基线 12 target 编译 |
| 3 | WP03/04 | 分步调度+批次体检最小实现 | import_session +5 API；import_preflight inspectBatch；CLI data preflight；测试×2；CMake 注册 | （未编译——首编进行中） | import_preflight.h 类内默认实参坑当场修复 | 编译后红→绿 |
| 4 | WP06 | fabricCachedPath 裸降级=真实洞 | object_store.cpp RemoteHttp typed 拒绝 + pinned case 更新 | （未编译） | 站点审计全表（WP06_REMOTE_AUDIT.md） | 编译后验证 |
| 5 | WP02/11 | abandoned worker 可观测缺口 | job_engine.cpp stuck>0 → trace 事件 | （未编译） | TraceEvent.detail 在位 | 编译后验证 |
| 6 | WP07/10 | 互操作 pin + 真实推理闭环 | 测试×2（infer 门控 SICNU_HAS_OPENCV） | （未编译） | #1558/1575/1576/1577/1579/1583-1586/1603-1605 实码核验=已修（B2 表） | 编译+跑 |
