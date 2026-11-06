# WP01 取消失败簇 — 交错矩阵与分类框架

## 目标复现套件（基线，未经本轨修改）

test_job_engine, test_r27_t02_job_engine, test_task_center, test_task_center_12,
test_execution_plane(基础), test_execution_plane_7/8/9, test_concurrency_stress,
test_core_concurrency_stress_r4, test_workflow_cancel, test_workflow_execution_plane,
test_agent_ops_task_center_executor, test_classification_task_center, test_ui_task_center_contract

## 交错表（构造目标：terminal 恰一次 / 迟到 cancel 不改写成功 / 取消不发布半成品）

| # | 交错 | 期望不变式 | 控制手段（不使用 sleep） |
|---|---|---|---|
| I1 | queued → (cancel 请求到达于 worker pick 之前) | Cancelled 终态恰一次；无 executor 运行 | 提交前预置 cancel / barrier 控制 pick 前窗口 |
| I2 | running → cancel → 协作算子观察到 flag | Cancelling→Canceled 恰一次；cancelRequestStamp 可观测 | latch 持住算子直到 cancel 请求被登记 |
| I3 | running → cancel → 算子忽略 flag（native 阻塞） | cancel watchdog 到期 finalize Canceled（typed reason 保留）；stuck 数可观测 | 注入永不 poll 的算子 + 缩短 setCancelWatchdogMs / setShutdownJoinTimeoutMs |
| I4 | 成功返回后 cancel 请求到达（迟到取消） | 终态 Succeeded 不被改写（RIO-10 已有 engine 层回归；TaskCenter 层待验） | barrier 等 record= Succeeded 后再 cancelTask |
| I5 | publishing（OutputCommitter 提交中）→ cancel | 已开始的事务性提交要么完成要么整体回滚；不出现 success marker 指向缺文件 | 注入 commit hook 卡点 + cancel |
| I6 | shutdown 与 running cancel 竞争 | Shutdown reason 与 User reason 不互相覆盖；shutdown 有界 | 同线程并发 shutdown+cancel + 小 deadline |
| I7 | 通知重放：listener 迟到重入 / observer 退订后 | 终态通知恰一次；退订后不再回调 | token 移除 + 终态后 removeWatch |
| I8 | 多级 DAG cancel 级联（父 cancel → 子孙） | 级联内每任务恰一次终态；无孤儿 Cancelling | pipeline + cancelPipeline + watchdog 兜底校验 |

## 分类标签（每个失败必须落袋）

A=真实 state-machine bug；B=通知丢失；C=fixture 失效；D=过紧 wall-clock；E=测试根本未运行（0-test/未注册）；F=非本轨根因（HANDOFF）

## 已知先验（写码前不采信，复现后核对）

- #1812 修复记录里点名 "task-engine cancellation debugging" 遗留
- #1558：test_e2e_open_issues.cpp 恒真断言——master HEAD 实读已由 #1812 重写为可失败断言（ISSUE_TRIAGE B2 表）；本轨仅复核，不改该文件
- #1699：fast/ASan 层墙钟阈值断言簇（涉及取消簇套件时一并治理）
