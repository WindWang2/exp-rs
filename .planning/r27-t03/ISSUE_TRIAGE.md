# R27-T3 Issue 分流（全部经 base 源码逐条复核）

| Issue | Sev | 复核结论 | 证据（base 10652fd0） | 处置 |
|---|---|---|---|---|
| #1733 AS-007 | P0 | LIVE | session_surface.cpp:207-238 approve_repair 对任意 driver 开放 mint，now/ttl 由调用方供给；repair_approval.cpp:20-29 digest=sha256/16 无密钥；ops_driver.cpp:431 直通；mcp_server.cpp:873-895 MCP 可达 | 修复 |
| #1741 AS-013 | P1 | LIVE | workspace_containment.h unknownDriverPayloadIsDangerous 只查首段；GTIFF_DIR:1:/abs、NITF_IM:0:、HDF4_SDS:…" 引号内嵌、/vsicurl 组合均漏 | 修复 |
| #1742 AS-006 | P1 | LIVE | dock startWorker (agent_ops_control_center_dock.cpp:193-235) 旧线程进 reaper 异步 join，新 worker 立即起，coordinator 无锁状态可并发 | 修复 |
| #1760 AS-008 | P2 | LIVE | dock armRepairApproval:316-332 恒返回 {} 成功，worker 期 qWarning | 修复 |
| #1761 AS-009 | P2 | LIVE | external_process.cpp:130-136 拒绝表仅 LD_*/DYLD_* 枚举 | 修复 |
| #1762 AS-010 | P2 | LIVE | secret_redactor.cpp:18-31 needles 无连字符形、值不扫 | 修复 |
| #1763 AS-014 | P2 | LIVE | agent_copilot_dock_widget.cpp:625-690 单调用直接 submit，无 containment/remote-deny/写审批 | 修复（门建在 src/agent/**，dispatcher 只读） |
| #1796 SPP-1 | P2 | LIVE | worker_daemon.py:558-672 仅 FileNotFoundError 回收 shm | 修复 |
| #1797 SPP-2 | P2 | LIVE | worker_daemon.py:447-468 initGui 失败→孤儿注册 | 修复 |
| #1798 SPP-6 | P2 | LIVE | rs_pipeline_runner.cpp:229+ 未检 root/steps/step/operator 类型；main_cli.cpp:507,570 无 try/catch → terminate | 修复 |
| #1799 SPP-7 | P2 | LIVE | plugin.py OPERATORS 进程全局，publish_to_iface 发布全部 | 修复（按声明模块限定） |
| #1802 P3 汇总 | P3 | LIVE | AS-011 mcp_bridge.ts:274-293 全量 env；AS-015 llm_streaming_client.cpp:66-69 非回环 http 带 Bearer；AS-016 canonicalPathWithin TOCTOU | AS-011/015 修复；AS-016 记录残余（见 RISK_REGISTER） |
| #1806 P3 汇总 | P3 | LIVE | SPP-3/5/8/11 小修可行；SPP-9 部分（size 校验）；SPP-10 签名体系/SPP-12/SPP-13 评估后取舍 | 修复可行子集，其余记录 |

## 决策（DECISIONS 摘要）
1. #1733: mint 仅存于 OperationsCoordinator（进程内随机 HMAC-SHA256 密钥）；session surface
   与 OpsDriver 的 approve_repair 一律 typed 拒绝 APPROVAL_HUMAN_CHANNEL_ONLY；dock（本地可信 UI）
   是唯一 mint 通道；TTL 上限 30min（超过即 mint 拒绝）；token 加 nonce；consumed 环按过期逐出、
   满且未过期时 arm fail-closed。
2. #1763: 单调用门 = workspace containment + remote default-deny（复用 surfacePathOutsideWorkspace）
   + 写类调用走既有 plan 审批卡（人工 Confirm）。
3. #1742: dock 持久单 worker 线程 + 串行任务队列（取消旧任务、排队新任务），GUI 不阻塞，coordinator
   单线程访问不变。
