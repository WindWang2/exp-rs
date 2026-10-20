# R19-02 RISK_REGISTER

| ID | 风险 | 区域 | 缓解 |
|---|---|---|---|
| R1 | merge-loss 再发生（29596c913 类）：PR 合并把已修复内容静默回滚 | 全仓 | T3 检测器 + ORACLES 中"已合并不变量"由既有测试守护（每轮跑本轨测试族） |
| R2 | git fetch HTTPS 间歇 SSL 中断 → 基线陈旧风险 | 流程 | `gh api branches/master` 核对远端 SHA；推送前再次核对 |
| R3 | 共享机器 -j2 超限（其他并行 agent 同时构建） | 构建 | 全局锁 `/home/kevin/project/exp-rs/.git/r19-global-j2-build` + resource_guard --wait 3600 |
| R4 | ninja 用户别名 -j40 误用 | 构建 | 一切构建经 resource_guard 显式 `--parallel 2` 钳制 |
| R5 | #1663/#1664 合并后 rebase 冲突 | 交付 | 冲突仅按文件语义人工式解析，禁 -X ours/theirs；改前 fetch 复核 |
| R6 | T1 取消语义改动破坏既有 cancel 测试（test_workflow_cancel.cpp 等） | T1 | RED 先行；全量 workflow 族回归 ×2 |
| R7 | T3 检测器误报（合法合并=取一侧的文件，如纯删除侧） | T3 | 检测"独占修改侧内容消失"而非"任意取一侧"；真实历史 29596c913 与 benign 合并双向校准 |
| R8 | RateLimiter 反向进度抑制（无生产者，行为记录） | surface_progress | 文档化，不改（YAGNI）；若未来出现多阶段任务需 revisit |
| R9 | get_tool_schema 每调用投影无缓存（B11 记账，无实测瓶颈） | mcp_server | 仅在 benchmark 证明 p95 超标时立项 |
| R10 | temporal phenology 工具未接线暴露无锁 static store（接线前无害） | spatial_tools | DEBT 记录；接线工作属 D-160-1 后续 |
| R11 | CI 全仓失效（#1491，2026-10-11 起） | 交付 | 全部本地验证 + TEST_RESULTS 全量记录（任务书允许） |
