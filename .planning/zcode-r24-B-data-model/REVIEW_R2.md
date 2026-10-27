# R24-B REVIEW_R2 — 独立评审（第 2 轮，换上下文）

- Reviewer：第二个 fresh-context 子代理，复核视角 = R1 修复验证 / merge-loss / 跨平台 / 契约完整 / 调用链 / 对抗测试健壮性；全量 diff + 24 个改动文件用项目实际 MSVC 编译旗标做语法级编译（全 clean，含当时进行中的完整构建前 ~2000/3515 无错）。

## R1 修复验证：8/8 CONFIRMED

finalRunId（keepOriginalRunId 与 fresh-id 两路径均验证）；尾批 flush（证明 flushPendingBatch/flushBatch 每条出口清空五个 batch 向量 → 新调用仅在 bug 场景可达、无双 flush、均在 try 块内）；QDir::Hidden（通配不误伤旧固定名）；unbucketed -1（与网格门精确同条件、round-trip 一致）；scene stage 唯一化；OOM 针词；QLockFile 6h（对照 Qt qlockfile 源码验证：本地活 pid 永不 stale、死 pid 立即回收、6h 仅约束异机兜底；进程内竞争仍由 g_publishFences 先拦）；错误先取后回滚 + legacy 形状守卫。

## 新发现与处置

| 级别 | 发现 | 处置 |
|---|---|---|
| **P1(测试)** | schema-gate 测试损坏 size/2 处——落在 gate 永不读取的页；实证 gate SELECT 仍成功 → open 走前向容忍分支返回 true → CHECK_FALSE 确定性失败 | ✅ 已修：改为砸第 2 页（ds_meta 根页）（commit c0bfaf7f5） |
| P2 | racer 线程内 `diagnostics().first()` 无空检查（当前全路径带诊断，属潜在 UB） | ✅ 已修：4 处加 isEmpty 守卫 |
| P2 | QLockFile 非竞争失败（目录只读/不存在）误报 AlreadyRunning | 📋 记录：现有调用点都先 mkpath+staging，不可达；不改为避免行为面扩大 |
| P3 | commitVersionImpl 把 step 失败与 0 行更新混同为 conflict（I/O 错误得 bogus 消息） | ✅ 已修：拆分两条消息 |
| P3 | 尾批修复后 R20-02 死代码块 + 旧测试名误导 | ✅ 已修：注释收缩为 belt-and-braces 说明；测试更名为 re-attaches 契约 |
| P3 | `.morph~` 崩溃残留不被 sweep 覆盖 | ✅ 已修：sweep 增加 `*.morph~` 模式 |
| P3 | stage/commit/addAssets 事务内做 CPU 工作（解析/指纹）加宽写锁窗口 | 📋 记录：正确性优先的取舍，PR 注明（大清单可感知，busy_timeout 5s 兜底） |

## 专项结论

- **Merge-loss**：tests/CMakeLists.txt diff 为纯追加（唯一 `-` 行是 diff 头），单 hunk 位于 R23-E 块后；中途的双 discover 错误已在 4cd86bbf 折叠修复；test_experiment_evaluation.cpp 改动限于一个 TEST_CASE（契约变更，期望与新实现一致）。
- **跨平台**：clean——/utf-8 全仓生效（µm 字面量安全）；QDir::Hidden 于 Windows 无害/POSIX 必需；QLockFile/setFileTime/portable::pid/SQLite::SQLite3 全跨平台；折叠 fence 键在大小写不敏感 FS 上同折叠锁名。
- **契约**：仓内全部 14 处 ReproductionBundleImporter 构造用单参 ctor（不变，m_datasetStore=nullptr → 数据集侧维持命名缺口）；仅新测试用双参。capsule 测试从数组重建 digest（自适应，无硬编码 digest）。
- **调用链**：addSourceAssetsToDraft 0 行→conflict 的 4 个 sample_promotion 调用点均按已报告失败处理（#1702 语义）；bucketOf 签名改动文件内唯一调用已同步；守卫成员改 unique_ptr 后所有用法为纯局部对象。
- **对抗测试**：racer 门增在线程首语句、线程内只用普通赋值（无 Catch2 宏跨线程 UB）；sweep 测试回拨 1 天对 addDays 算术 DST 安全；QTemporaryDir 全覆盖无泄漏。

## 结论

R1 8 项全部确认正确；本轮 1 项必改（已修）+ 2 项已修打磨 + 2 项记录。生产 diff 判定 solid，进入构建后验证。
