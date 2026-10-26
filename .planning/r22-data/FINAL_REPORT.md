# R22-02 (r22-data) FINAL_REPORT — 空间数据访问安全、远程 COG 缓存、资产一致性与可复现数据血缘

**分支**：`agent/r22-data-20261025T132358Z`（base = origin/master @ `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`）
**提交**：44f067a709（主修复）→ d3f8667bb7（测试）→ b8cb89844f（文档）→ 715414f35d（对抗扩展）→ f22ec241bc（R1 轮）→ 5fc544a6c6（R2 前置加固）→ eb24ccb4dd（R2 轮）

## 交付内容（6 Issue 全闭环）

| Issue | 修复 | 决定性 oracle |
|---|---|---|
| #1679 P0 SSRF | authority 以首个 `/?#` 终止 + `rfind('@')` 剥 userinfo（与 curl/RFC3986/urlHostOf 一致）；DNS 分支统一 isPrivateIpv6；resolveForPinning 在进程策略下自行拒绝私网字面量 | 独立 RED 程序：master 7/9 漏判 → 修复 9/9 拒绝；16 扩展形态 curl-parity 分析；进程内 232/27 SSRF 族全绿 |
| #1700 P1 磁盘缓存几何键 | identityBasis 含 `bs=<blockSize>`；几何变化清内存+磁盘层；loadBlocksFromDisk 拒超长块；R1 后比较/应用同锁 | store 层 oracle（同 URL+ETag、64K put vs 128K read 必须 MISS）+ basis 格式断言；327/39 全绿 |
| #1701 P2 崩溃一致性 | marker rename 检查（失败保留 journal+.bak 抛错）；journal 写后 good() 校验；陈旧 .bak 清扫（R2 加固：target 缺失时复活唯一副本，绝不删除） | 真实机制注入（目录占位）：marker 失败/陈旧 .bak/孤儿 .bak 三窗口均有决定性断言（master 上失败） |
| #1680 P2 并发发布 | rename 失败且对象已存在 → continue 重走 digest 验证；tmp 名 pid+原子计数 | start-gate 双线程 8 轮 ×（R2 10/10 连跑）全收敛；Qt rename 前提实证 |
| #1702 P2 provenance | 4 处 addSourceAssetsToDraft 全检查，失败报 promotion_partial；[[nodiscard]] | SQLite 直写损坏 manifest 注入 → master 静默成功 vs 修复后如实失败（断言在 master 失败） |
| #1703 P3 债务 | validateWindow int64；stepFailed 误报区分（4 处同型）；readMask 范围注明（issue 前提被证据推翻：6 处测试调用） | INT_MAX 边界窗拒绝（master 通过校验）+ 正常窗不回归（2094/16 契约族全绿） |

## 双轮独立 Review

- **R1**（分离上下文 subagent，自带 9 套件 ~12k 断言复跑）：**APPROVE**，8 findings（F1 P2：#1700 集成测试 vacuous——已修：store 层 oracle + 文档纠错；F2 P3 锁域折叠、F5/F6 测试强化——全闭环）。
- **R2**（另一 subagent，不采信 R1 结论）：**APPROVE** @ 5fc544a6c6。**确认 R1 修复轮曾引入一个真实 P1 数据丢失缺陷（孤儿 .bak 唯一副本被清扫删除）**——该缺陷在 R2 出报告前已被本轨主动发现并修复（5fc544a6c6，复活语义 + 决定性测试 eb24ccb4dd）。R2-2/3/6 follow-up 全闭环；R2-8（stats 计数 quibble）与 R2-4/5/7 如实记录不修（理由见 REVIEW_R2.md）。

## 验证

15 个测试族全绿（详见 TEST_RESULTS.md）；关键安全/崩溃恢复/数据一致性族全部双跑（部分三跑）；R2 独立复跑 + 10/10 连跑。未触碰他轨文件（R2 白名单复核 PASS）。

## 未做（NOT_STARTED / NOT_EXECUTED，诚实声明）

- B3（catalog/导入全链路）、B5（10 万级性能压测）、B6（系统接口真值）：本轮聚焦 B0/B1/B2/B4 的 6 个已验真 issue，未启动；候选清单已入 CROSS_TRACK_FINDINGS.md（含 #1587/#1548/#1545）。
- GPU/TSan/Windows/macOS/线上 CI：无环境/按任务书不参与。

## 遗留风险（RISK_REGISTER 对照后仍开放项）

- #1679：`isPrivateHost` 与 `urlHostOf` 仍为两份实现（percent-decode 语义有差异，所有分歧方向已论证 fail-closed）；统一为单一解析器是后续清理项。
- #1701：跨进程并发 publish 同一目标仍无进程间锁（master 既有；本轨未扩大也未解决）。
- #1700：几何切换 clear() 顺带丢弃同几何可用块（冷启动回填代价，正确性无虞，注释已注明）。
