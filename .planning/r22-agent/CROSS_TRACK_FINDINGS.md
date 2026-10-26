# R22-04 (r22-agent) CROSS_TRACK_FINDINGS

## 对其他轨道的发现（本轨不改其归属文件）

1. **TaskCenter（processing 轨）`m_forwardedLogCounts` 与有界日志的交互**：#1705 有界化后，cumulative snapshot 的 logLines 可能小于 TaskCenter 已转发计数（旧计数指向被驱逐的索引空间）。TaskCenter 现有逻辑（task_center.cpp:2039 `else if (record.logLines.size() > forwarded)`）天然跳过，无错误转发；delta 路径经 cursor 补偿后仍严格连续（本轨 test_r22_agent_job_logs 有专测）。若 processing 轨希望 UI 提示"日志已丢弃 N 行"，可消费新增的 `JobRecord::droppedLogLines` 字段（job_types.h）。
2. **worker_daemon.py 归属**：见 RISK_REGISTER 第 1 条——daemon 侧 1MiB 门是对端协议修复，宿主侧独立成立。
3. **#1675 (R21-C)**：其对 mcp_server.cpp 的 parameters 类型门与 execution_id 取消映射与本轨 StdinReader 改动无 hunk 重叠；建议 merge 顺序任意。
4. **pi/ 轨**：本轨未触碰 pi/**；#1704 的修复对 pi bridge 透明（沙箱在宿主侧）。
