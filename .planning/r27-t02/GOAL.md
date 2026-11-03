# GOAL — R27-T2

基于 master `10652fd003` 修复并深化数据资产 / 遥感 I/O / 分块与模型执行运行时的可靠性与可恢复性：

1. **A（P1）** #1743：`ChunkGraph::addJoin` 重复输入 fail-closed + 空 join 运行时拒绝（RIO-3），附 graph/property 反例矩阵；合法 join、cancellation、backpressure 不回归。
2. **B（P2）** #1764 租约表有界；#1766 resume 输出路径独占租约（单拥有者，另一个 fail-closed，崩溃确定性恢复）；#1767 shutdown 有界 join + 可选 per-job 超时。
3. **C（P2）** #1765：STAC COG 安全后端打开入口（offline/SSRF/redirect/DNS-pin/range-cache），RIO-6 RemoteDatasetPool 接同一受控路径。
4. **P3 清扫** #1803 白名单内 9 项（RIO-3,6,7,9,10,11,12,13,14）。
5. **D 深化**：四状态导入会话（检查/继续/取消/恢复）+ 机器可读操作摘要（复用 ImportPreflight/DataManager，消除重复身份维护）。
6. **E 验证**：tile 尾批无丢块、取消语义、故障注入（journal 撕裂/损坏块/磁盘错误）、资源峰值有界的针对性测试与记录。

交付：唯一 PR `agent/r27-t02-data-io-runtime-reliability-20261102T132440Z` → master，两轮独立评审通过，关键测试双跑绿。
