# R22-02 (r22-data) TASKS / ORACLES / RISK

## TASKS（本轮里程碑 M1：P0/P1/P2+P3 闭环）

- [ ] T1 (B0/#1679)：统一 authority 解析 —— isPrivateHost 与 urlHostOf 共用「先 `/?#` 截断 authority、再 `rfind('@')`」语义；resolveForPinning 对 IP 字面量自行 isPrivateIpv4/6 判定（纵深）；DNS 分支 IPv6 判定改用 isPrivateIpv6；回归测试 `?@`/`#@`/多 `@`/userinfo/IPv6/编码/端口。RED：现 master 上 `http://127.0.0.1?@example.com/` 判公网（已有 issue 证据 + 新用例先行）。
- [ ] T2 (B1/#1700)：blockSize 纳入磁盘缓存身份 —— 块文件键或块头含 blockSize 且读取校验；loadBlocksFromDisk 校验块长（非末块 == blockSize，任何块 ≤ blockSize）；updateConfig geometry 变化时失效磁盘层该资源（或全局 invalidate）。RED：块写盘→改 blockSize→跨进程读 → byte-for-byte 不符（构造 fixture 演示，不需网络）。
- [ ] T3 (B2/#1701)：commit-marker rename 检查失败（失败时保留 .bak、抛 IoError，不删任何恢复材料）；writeJournalFile 检查 out.good()/close；写 journal 前清各成员陈旧 .bak。fault-injection 用现成 SICNU_FAULT_POINT 机制扩展。
- [ ] T4 (B2/#1680)：put 的 rename 失败分支 → 若 objectPath 存在则 continue 重走 digest 验证；tmp 名加随机/线程后缀。并发测试：双线程/双实例 put 同 digest 均成功或至少一方成功且对象健康。
- [ ] T5 (B4/#1702)：4 处 addSourceAssetsToDraft 检查 Result，失败 → promotion_partial 语义诊断（与 addSamples 失败路径一致：失败即报告，不静默成功）；[[nodiscard]]。
- [ ] T6 (#1703)：① validateWindow int64 化；③ addSourceAssetsToDraft 的 stepRow 失败与「版本不存在」区分错误码；② readMask 现状记录（无调用者→保守不删公共 API，只在头文件注明，或若有测试覆盖则改语义）——以证据定。
- [ ] T7：测试注册 + 全部相关域族绿 + 关键安全/崩溃恢复双跑。
- [ ] T8：R1/R2 独立 review + 修复闭环 + 交付文档 + PR。

## ORACLES（可验证完成条件）

1. O-1679：新回归用例覆盖 issue 全部 URL 形态，全部判私网/fail-closed；既有公网用例（example.com、真实 fixture server loopback 例外路径——测试服务器本身是 127.0.0.1，需走测试白名单机制，确认现有测试如何处理）不回归。双跑。
2. O-1700：同 ETag、blockSize 64K→128K 后，磁盘层不返回旧块（loadBlocks 校验拒绝或键分离）；byte-for-byte 与可信源一致用例。
3. O-1701：注入 commit-rename 失败 → publish 抛错、.bak 与 journal 保留、recover 后数据完整（新值或旧值，无半态/无删除已发布数据）；注入 journal 写失败 → 抛错而非静默。
4. O-1680：并发 put 同 digest，双方结果均成功（或重试后成功），池内对象 digest 正确。
5. O-1702：使 addSourceAssetsToDraft 失败（非 draft 版本），promotion 报告失败诊断而非成功。
6. O-1703：溢出窗口被拒（int64 边界用例）；错误码区分。
7. 全部相关测试目标两遍全绿；`git diff --check` 干净；未触碰禁区文件。

## RISK_REGISTER

| 风险 | 缓解 |
|---|---|
| 网络间歇 → fetch/push 失败 | gh api 兜底核 SHA；push 有限重试；失败保 commit 并记录阻塞 |
| 共享构建机资源紧张 | 双锁 + -j4 上限 + 只建最小目标；free/ps 记录 |
| vendor GDAL/Qt 伪失败 | 沿 R21-B 已验证配方（同 vendor 前缀 + link-shim）；环境红单独记录不冒充回归 |
| #1673 后续更新触碰 dataset_store.cpp | push 前重查 files；hunk 级合并；必要时 rebase |
| 测试 loopback 服务器本身用 127.0.0.1 | 现有 SICNU_OFFLINE_GUARD/白名单机制已处理，沿用；不新增网络依赖 |
| readMask 删除影响隐藏调用者 | grep 全库证据为准；有疑虑则只修溢出/注释不动 API |
| 上下文压缩丢失进度 | LOOP_LEDGER.md 每轮追加；git commit 粒度小且可回滚 |
