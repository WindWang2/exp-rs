# R25-B ISSUE_TRIAGE（执行时实查）

基础：r23-B 已对 144 open issues 全量分诊（.planning/zcode-r23-B/ISSUE_TRIAGE.md，in master），结论为 B 域全部 FIXED_OPEN（已修复未关闭）。本轨对任务书点名 Issue 重新实码抽验：

| Issue | 标题要点 | 状态 | 证据 |
|---|---|---|---|
| #1541 | 检测 lane 缺 CV_CN_MAX 护栏 | FIXED_IN_MASTER_BUT_OPEN | detection_tile_engine.cpp:331 / tile_inference_engine.cpp:1265 均有 `bandCount > CV_CN_MAX` typed 拒绝 |
| #1545 | range cache 探针不传播 SSRF 防护 | FIXED_IN_MASTER_BUT_OPEN | r23-B #1723 W4：probeRemote 切换 httpFetchStatus per-hop guard + DNS pinning（PR body 明示） |
| #1546 | repeat_execution 变量遮蔽 | FIXED_IN_MASTER_BUT_OPEN | repeat_execution.cpp:20-70 现代化重写（recorded/repeat 命名区分，无遮蔽模式） |
| #1547 | 非身份维度误判 deviated | FIXED_IN_MASTER_BUT_OPEN | replay_deviation.cpp:119-143 kIdentityDimensions identity-only gate + 诊断维度分离 |
| #1548 | 身份解析冷路径自死锁 | FIXED_IN_MASTER_BUT_OPEN | remote_identity_resolver.cpp:175 / execution_identity_bridge.cpp:164-168 "probe unlocked" 注释与 TTL 重探结构 |
| #1581 | lineage 切片丢方向 | FIXED_IN_MASTER_BUT_OPEN | r24-B #1719 WP-B7：方向保持导出+导入重挂 |
| #1637 | geospatial 技术债 4 项 | PARTIAL(sub-4 已修) | sub-4 probe 首跳=per-hop 已修（#1723）；其余 sub 项以 r23-B triage 为准 |
| #1647 | src/python 技术债 | OUT_OF_TRACK | src/python 非 B 轨写区 |

结论：本轨不重修以上任何项；新缺陷以本轨测试/评审发现为准。144 条 open issues 的整体状态与 r23-B 分诊一致（B 域已修未关），本轨不批量关 Issue。
