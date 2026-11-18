# R33-T05 ISSUE_TRIAGE — 与本轨相关的开放 issue 定位

开放 issue 总数 106（2026-11-18）。本表只列与本轨写集相关项；其余归各轨。

| Issue | 判定 | 说明 |
|---|---|---|
| #2079 agent-ops DecisionRecord inputs 内层类型校验 | OUT_OF_TRACK（decision_record.cpp 为 #2134 刚合并领域；本轨写集明示不改） | 需要时跨轨 HANDOFF；#2134 已触碰该文件，先核验是否已修 |
| #2078 OperationsCoordinator 校验失败仍消费 repair approval | OUT_OF_TRACK（operations_coordinator.cpp 本轨排除） | 同上 |
| #2080–#2085, #2073–#2077 agent/Pi/dock 系列 | ACTIVE_PR_COVERED→MERGED（#2134 领域） | 不重做 |
| #1932 agent-ops swarm 汇总 12 条 | NEEDS_REPRO（聚合条目，未逐子项核验） | 后续批次逐条对号 |
| #1931/#1802/#1628 历史聚合 | NEEDS_REPRO | 后续 |
| #2124 TaskCenter sidecar cache | OUT_OF_TRACK（T04 TaskCenter） | — |
| #2081 i18n gate | OUT_OF_TRACK（T06） | — |

本轨首批以 7.3 节 5 项静态候选为主（全部 SOURCE_CONFIRMED，见 LEDGER 第 1 轮），不占用上述他人领域。
