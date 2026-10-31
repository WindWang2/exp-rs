# ISSUE_TRIAGE — R25-E（对 144 open issues 的本轨相关切片）

| Issue | 状态判定 | 证据 | 本轨动作 |
|---|---|---|---|
| #1695-17c GUI 发现阻塞 | 主缺陷 FIXED(r24-c #1721)；残留 close-time join LIVE | teaching_admin_dock.cpp:446-462 注释自认 | W5 修复（取消钩子+复用扫描） |
| CT-C6 双扫描/无取消钩子 | LIVE | batch_assessment.cpp:589 内部重发现；discoverSubmissions 无 cancel 参数 | W5 修复（E 所有权后端 API） |
| CT-D1 run_lab.cmd fixture 规则评 foundry | LIVE | run_lab.cmd:37 + r23-D CROSS_TRACK 移交 | W5/W6 一行修复 + 回归 |
| #1653 verification beginResult 三重复制 | FIXED (51eb7d1a6，三 TU 均已 include checks_numeric.h，2026-10-29 复核) | 不重修；WP-E4 取消 | — |
| #1694 checkpoint 权威 | FIXED (#1711) | batch_assessment.h:98-108 注释 | 回归已有测试（test_r22_teaching_batch_regrade 等） |
| #1689 跨学生泄露 | FIXED (#1711) | feedback_pack.cpp compileForeignIdRegex | W7 回归 |
| #1678 NaN/Inf 答案 | FIXED (#1711) | session_state.cpp recordAnswer | W7 回归 |
| #1652-1 rubric schema 漂移 | FIXED (R20-05) | rubric_builder.cpp:168-207 引擎委托 | W2 审计确认（canonical schema 一致性） |
| #1652-2 feedback 前缀误报 | FIXED | feedback_pack.cpp:126-127 | 不动 |
| #1695-17a AsyncRunner 死代码 | 待核 | — | 侦察后决定（app/ 归 D 轨，暂不动） |
| #1695-17d 空白答案 | 待核 | session_state.cpp:351 只拒空串（r22 前）| W1 核验 |
| #1601 双验证器 | 分工已明确（verification=测试接线核 / verify=生产权威 ADR 0172）| CT-D3 | W3 记录不删任何核 |
