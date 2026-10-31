# TASKS — R25-C

| # | 任务 | 状态 | 证据 |
|---|---|---|---|
| W0 | 侦察+基线+triage+碰撞矩阵 | ✅ | BASELINE/ISSUE_TRIAGE/PR_COLLISION_MATRIX |
| W1 | 桥接 typed errors + serverInfo/generations + onGeneration + 壳层漂移报告 + r18c flake 修复 | ✅ 提交 6bf14aaf9a | pi suite 45/45 ×2 |
| W2 | algorithmEntryMap 投影 prerequisites/limitations/guarantees + 新测试 | 代码完成，待构建验证 | tests/test_r25_c_capability_honesty.cpp |
| W3 | run_id 诊断面（coordinator 只读方法 + MCP 分支 + resumable advisory + schema） | 代码完成，待构建验证 + snapshot 再生 | tests/test_r25_c_workflow_resume_diagnostics.cpp |
| W4 | r22 回归钉 + 恶意输入矩阵（E2E 拒绝链计入） | 待构建后运行 | — |
| W5 | 计划→结果可解释：W2 前置条件 + W3 恢复诊断即交付（诊断/确认型，无新自主服务） | 随 W2/W3 | — |
| W6 | 插件 worker 类型化错误：r23-E 已覆盖（E6xxx 全线）；本轨维持邻域回归 | 回归待跑 | — |
| W7 | 真二进制 E2E（成功/执行/拒绝/取消/崩溃→诊断→恢复 5 链） | 脚本完成，待 surface_mcp_host 构建 | pi/test/r25_c_e2e_real_host.test.mjs |
| W8 | R1/R2 独立评审 + PR | 待 | — |
