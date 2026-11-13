# R30-T05 REVIEW R1 — 独立审查记录

- 审查者：独立子 agent（新上下文，非实现者）
- 绑定：base 606e3c9101 → HEAD 21f157c530（审查期间树推进两次，findings 按最终 HEAD）
- 审查者独立执行：Pi 全套 88/88/0/0；drift+wait 复跑 14/14；E1/E2/E3-A/E3-B 全绿；journal bound 13/3；evict 141 + test_mcp_server 33 用例全绿。
- 结论：**APPROVE-WITH-FIXES**（无 P0/P1）

## Findings 与处置

| # | 级别 | 摘要 | 处置 |
|---|---|---|---|
| 1 | P2 阻塞 | crash-boundary retry/post-hoc refusal 零覆盖（所有 drift 测试从活 gen1 崩溃，crashCrossed 从不触发） | **修复**：新增 3 个用例（success-crossed→post-hoc SCHEMA_DRIFT；double-crossed retry 成功；always-crash 上限退出） |
| 2 | P2 阻塞 | post-hoc 拒绝丢弃已执行结果且未警告孤儿副作用；DECISIONS D1 承诺"明示可能已到达服务器"但文案没有 | **修复**：post-hoc gate 捕获 SCHEMA_DRIFT 后改写消息（明示 may already have executed + 检查孤儿 execution/artifact 指引） |
| 3 | P2 | wait 内 gate 自身 5s 等待不受 timeout_seconds 预算约束（最多超 ~5s） | **修复**：gateDispatch 接受 maxWaitMs；wait 循环传剩余预算 |
| 4 | P2 | docs/agent/pi-adapter.md 仍是 R29 契约（continuation/structuredContent/新错误码/wait timeout≠cancel 未写） | **修复**：同步文档 |
| 5 | P3 | gate 的 abort-race listener/timer 不清理（与 sleepBounded 同类） | **修复**：同 sleepBounded 纪律 |
| 6 | P3 | cursor 分支 continuation 从 next_offset 续读，未提示被截断丢掉的页首 | **修复**：文案提示 head 被丢、从 offset 重读可恢复 |
| 7 | P3 | structuredContent 镜像注释过强（数组/嵌套未封顶） | **修复**：收窄注释为顶层字符串封顶（行为保留 R29 语义） |
| 8 | P3 | 零 bridged 工具会话崩溃后 wait 永久 SCHEMA_UNVERIFIED | **修复**：无注册工具时跳过 gate |
| 9 | P3 | 信封字段按 first-match 独立提取，多信封场景可能拼合 | **处置**：注释钉住唯一性假设（今日 sha256 键仅 artifact_read 发射；reviewer 验证） |
| 10 | P3 | evict 用例 idless==recent 断言弱（SEAMS 信封同构） | **接受**：测试已自我文档化限制；前两断言有区分度 |

## 审查者验证为干净的领域（摘要）

gate 自旋守卫与 generation 竞态全交错追踪收敛；sleepBounded 泄漏已修并实测（21/21）；wait 状态词汇与 wire 集合完全一致；journal 有界读边界正确；fixture 修复符合文档化契约（throwIfCancelled/GDALAllRegister）；R29 测试更新合法非弱化；E2E 断言诚实（真实分页 sha256 重组、cancel/非-cancel 区分、skip 标 NOT_EXECUTED）；接口兼容（无改名、details 只增）。
