# ORACLES — r23-C 可检验验收条件

- O0 基线新鲜：BASELINE.md 记录 fetch 实测 SHA/PR/issue/worktree；push 前重新 `git fetch origin --prune` 复核。
- O1 RED 真实：每个 WP 的测试先于修复编译+运行并失败（WP-1 Case A/B 预期失败点明确列出）；若本就绿则视为测试未触发（变异验证）。
- O2 双 GREEN：受影响测试目标定向运行两轮全绿（GREEN1=修复后首跑，GREEN2=复跑排除侥幸）；同域 smoke（test_task_center 全量 + test_r22_agent_job_logs 全量）两轮。
- O3 消费契约反例：WP-1 断言集覆盖「不丢（每行恰一次）」「不重（迟 delta 零转发）」「正确切片（绝对区间逐行内容比对）」三轴。
- O4 构建：受影响目标编译 exit 0；`git diff --check` 干净；无禁用/跳过的关键测试。
- O5 双独立评审：R1、R2 两位独立 subagent（与开发上下文隔离）逐 hunk 审 `origin/master...HEAD`；P0-P2 全闭环并对修复提交复审。
- O6 冲突：push 前 `git diff --name-only origin/master...HEAD` 对照新 fetch 的 master；tests/CMakeLists.txt 仅 append 且兄弟注册块逐字节保留；无冲突标记。
- O7 权属：变更文件 ∈ OWNERSHIP 白名单。
- O8 交付：push 成功 + 非 draft PR（body 含 BASE/HEAD、范围、测试/RED-GREEN 证据、双评审、风险、issue 状态、重现命令）；不自动合并、不等 CI。

## 每轮 Oracle 验证命令
```bash
ctest --test-dir build-r23-C --output-on-failure --parallel 1 -R 'task_center'
./build-r23-C/tests/test_r23_c_task_center_log_spaces   # 直跑全量断言
git diff --check
```
