# [Pi/Bridge] startup-deadline 成功路径清理未回移 exp-rs-spatial.ts——事件循环每次 spawn 被钉住最长 30s（修复漂移）

P2
Affected Location: pi/exp-rs-spatial.ts:178-198（无 finally 清理）；对照已修复的 pi/mcp_bridge.ts:144-172
Root Cause & Impact: mcp_bridge.ts 从 exp-rs-spatial.ts 抽出时修掉了"健康 initialize 后 30s 定时器仍 armed、钉住事件循环"的问题（try/finally + cancelStartupDeadline），但修复未回移原文件。Pi 实际加载的是 exp-rs-spatial.ts（pi/README.md:31，jiti 直载），故生产路径带病：每次 spawn/respawn 后进程空闲退出被延迟最长 30 秒，respawn 频繁的会话叠加。subagent V 确认两文件为活跃分叉（spatial 有 mcp_bridge 缺的 fastCrashCount 熔断与启动竞态清理——双向漂移）。
Reproduction: spawn 完成后记录进程空闲退出延迟（现实现 ≤30s；修复版即退）。
Recommended Fix: 回移 mcp_bridge.ts:158-172 的 try/finally 形态；长期删除分叉（exp-rs-spatial.ts 引用 mcp_bridge.ts）并加防漂移测试。
