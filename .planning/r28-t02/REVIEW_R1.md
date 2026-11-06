# REVIEW_R1 — R28-T02（独立对抗评审，2026-11-06）

评审者：独立 subagent（general-purpose，全新上下文；与实现者不同会话）。评审方式：5 commit 全 diff + 逐文件实读 + 实际执行（重建基线 fixture 复现取消簇、直跑 5 个新套件 13/13、agent_ops 28/28、io_fabric 两套件、CLI `data preflight` 实调含未知 flag 拒绝、cube/execution_plane 系列基线对照）。

## 结论：**SHIP**（0×P0，0×P1，2×P2，8×P3）

## Findings 与处置

| Sev | 位置 | 发现 | 处置（全部完成） |
|---|---|---|---|
| P2-1 | object_store.cpp 注释 + cli cube/mirror 路径 | 注释宣称"CLI 有 CacheGuard"但 `data cube plan/window`、`data mirror materialize` 从不装 cache；WP06 后这些命令的远程资产从"裸读可用"变"逐资产 typed 失败"且无 CLI 补救 | ✅ 已修：cube/mirror 路径补 CacheGuard+install（同 prefetch 的 RAII 纪律）+ `--allow-private-network` 消费 + usage 字符串更新 + object_store.cpp 注释改为准确表述（"cache/cube/mirror 均装"） |
| P2-2 | fabricCachedPath 行为面 | 任何轨道外消费者若依赖"未装 cache 时 raw passthrough"将被打断（仓内调用方已审计全部安全；外部/二进制消费者不可穷举） | ✅ 记录于 HANDOFF.md"集成注意"+PR body 已知风险（回滚=单文件 revert）；无仓内受损路径 |
| P3-C | job_engine.cpp:197-206 | `shutdown_abandoned_workers` trace 无测试断言 | ✅ 已修：lifecycle_batch 场景⑥ 安装 RingTraceSink 断言事件存在且 detail 含 stuck=N |
| P3-G | cli qPreflightBatchToJson | 解析失败时空对象+exit Ok 的"假成功形" | ✅ 已修：*ok 出参 + conversion_failed 错误包络 + GenericError 退出码 |
| P3-A | import_session.cpp 成功路径 | resume 降级再注册项保留 stale message | ✅ 已修：Registered 成功时 item.message.clear() |
| P3-E | closed_loop.cpp:226 | .moc include 在 SICNU_HAS_OPENCV 守卫外（无 OpenCV host 未验证） | ✅ 已修：移入守卫内 |
| P3-H | PATH_OWNERSHIP.md | src/cli/cli_commands.cpp 改动未在所有权文档声明 | ✅ 已修：共享热点节显式声明（纯加法+D-9 决策） |
| P3 | WP01_CANCEL_MATRIX.md #1558 行 | 矩阵宣称本轨修 #799 恒真断言（实际 master 已由 #1812 重写） | ✅ 已修：改为复核表述 |
| P3 | lifecycle_batch 头注释 | "exactly-once" 表述强于证据（无通知计数 oracle） | ✅ 已修：措辞对齐实际 oracle（每任务终态+迟到取消不改写） |

## 评审者实证亮点（作者采信）

- 在基线 fixture 上重建并复现了 TC_CNC_01/03/05 的 `"succeeded" == "cancelled"` 失败——独立确认取消簇分类（fixture 违反 RIO-10 协议）诚实；
- 确认 mock 修复零断言增删（非弱化）；确认 identity_token journal 往返、verifyFacts 幂等不动点（手推全部序列）、stepwise API 无 Importing 悬空路径；
- 实调 CLI：`data preflight` 对坏路径/缺路径/未知 flag 的 typed 行为全部符合声明；
- execution_plane/task_center 系列在评审者沙箱有 helper-spawn SIGABRT 环境性失败（基线同样失败=非本轨回归；本机两次全绿）。
