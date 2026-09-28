# GOAL-LOOP LEDGER — agent-harness-r4 (Track 8: Agent Harness Adversarial Hardening, R4 Deep)

Branch: hardening/r4-agent-harness · Base: origin/master 15e5c66b5 (2026-09-26)
Ledger at .planning/agent-harness-r4/00-ledger.md per current repo convention
(recent tracks commit their track ledger; root .goal-loop-ledger.md is a stale
older-track artifact, left untouched). Token 口径见 PLAN.md 末节：subagent 用量
精确，主线为估算，不为凑预算注水。

| 轮次 | 改动 | 验证命令 | 结果 | 本轮 tokens(估) | 累计 tokens(估) |

（口径：subagent 用量为精确值；主线为按活动量估算。总计约 33.4M —— 低于任务书
280M 预算锚定，原因如实记录：①本环境每个 cmake configure 触发 ~2600 边全量
重编（sicnu_feature_probes.h mtime），引导构建占用大量墙钟但 token 主要为工具
回报；②任务书 3.2 交付下限（8 类/20+ 用例/10 提交/12 文件/语料复用）已全部
达标并经评审复核，未为凑预算注水。）
|---|---|---|---|---|---|
| 0 | fetch+worktree；锚定表复核（259/47+46/9+3 实证）；#1334-#1338 overlap map（#1334 harness 重叠=0）；WP 前提修订（BASELINE §6/§8）；BASELINE/PLAN/DECISIONS/ledger | git/gh/find/stat 实测 | PASS | ~2,300k | ~2,300k |
| 1 | 探缝（Explore 子代理 782k 精确计量）：LLM 产出链/持久化面/工具分派接缝定位；probeModelManifest array-vs-object 缺陷发现（#1337 在修，WP-D 收窄并记 EVIDENCE） | rg+通读实证 | PASS | ~1,600k | ~3,900k |
| 2 | WP-A 计划层：对抗矩阵测试（3 钉+4 新合同红）+ agent_plan 类型加固/重复输出探针/4096 步上限/O(n)接线；修复自引入的前向引用回归（预收集 id 集） | 待编译验证 | PENDING | ~1,100k | ~5,000k |
| 3 | WP-A SSE 层：malformedToolCall 类型化信号+1MiB/8MiB 上限实现；dispatcher 幻觉/缺参/未知字段钉测；既有 #701 测试兼容性核对 | 待编译验证 | PENDING | ~900k | ~5,900k |
| 4 | WP-B/C/D/E/F/G 测试文件全量落盘（verifier_robustness/ledger_completeness/grounding_evidence/runloop_recovery/harness_boundaries + 语料+loader+双消费者）+ 6 目标 CMake 注册 | 待编译验证 | PENDING | ~1,400k | ~7,300k |
| 5 | 构建引导攻坚：发现①会话进程组会 SIGHUP 掉后台 ninja（对策 setsid+nohup 脱管）②master 上 test_llm_streaming_client/test_tool_call_dispatcher 链接 sicnu_agent.so 缺 agent_loop 符号（补 sicnu_agent_loop，tests/ 白名单内；#1335 在修生产侧）③本环境每次 cmake 重配置刷新 sicnu_feature_probes.h mtime → ~2600 边全量判脏（教训：CMakeLists 定稿后再 configure，一次成型） | ninja 全量 pass | IN PROGRESS | ~600k(估) | ~6,500k(估) |
| 6 | 编译修复四连：include 路径、evidence:: 限定、rvalue HarnessError、spatial_tools 命名空间；8/8 测试对象编译通过；分 9 个原子提交（C2-C10）落盘 | ninja 对象级验证 | PASS | ~900k(估) | ~7,400k(估) |
| 7 | 独立对抗性评审（1 subagent，24.3M tokens）：18 发现（8 P1 + 10 P2）→ 全部处置：8 P1 修复（含语料路径死锁、oversized fixture 红、envelope 形状错）、语料转义重构、REVIEW_LOG/EVIDENCE 归档；车道 ctest 双跑 98%（123/126，3 失败均非本轨道：1 未构建兄弟目标 + 2 #1335 在修教师令牌） | ctest 双跑一致 | PASS | ~26,000k | ~33,400k |
