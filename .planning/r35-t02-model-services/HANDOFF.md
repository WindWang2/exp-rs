# HANDOFF — R35-T02

## 本 PR（#2152）已交付

- WP01 readLine 绝对单调 deadline（python_worker_provider.cpp）＋4 fixtures＋4 cases
- WP18 ensemble acquire 取消传递（model_ensemble.cpp）＋1 barrier case
- WP05 provider 旋钮 typed（model_catalog.cpp + python 工厂域检查）＋2 cases＋docs
- RED/GREEN/R1/R2 全记录：LEDGER.md / ACCEPTANCE.md / REVIEW_R1.md / REVIEW_R2.md

## 对其他轨

- 无新增跨轨 API；acquireWithFallback 取消谓词为既有参数，T04/T05 消费方不受影响。
- registry waiter 取消语义（100ms 轮询）为既有行为，本 PR 仅让 ensemble 调用方开始使用。

## 下一会话恢复

1. `source .planning/r35-t02-model-services/RESUME.md` 的路径；base 以执行时 origin/master 为准（本 PR base=120677e6）。
2. 若 #2152 已合并：从最新 master 重建 worktree；未合并：不堆叠，另开新 worktree 新分支。
3. 优先候选见 LEDGER「后续候选」；GPU leaf 合同裁决（WP09）需先双测试固定事实。
