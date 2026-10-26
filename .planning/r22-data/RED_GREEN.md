# R22-02 (r22-data) RED → GREEN 证据

方法：不建第二个 worktree。RED 用 `git show origin/master:<file>` 导出 blob 在 /tmp 独立编译运行（#1679），或以「master 代码路径 × 新断言」的逐行矛盾证明（其余）；GREEN 为修复后测试全绿且关键组双跑。

## #1679（P0 SSRF authority 解析）

**独立可运行 RED**（`/tmp/r22red`，`g++ -std=c++20`，offline_gate_master.cpp = origin/master blob，cpl_conv.h 打桩）：

```
$ ./red_master            # @911e0b17 原版
1 http://127.0.0.1/
0 http://127.0.0.1?@example.com/          ← 漏判（curl 实连 127.0.0.1）
1 http://127.0.0.1/?@example.com/
0 http://127.0.0.1:8080?@example.com/     ← 漏判
0 http://169.254.169.254?@example.com/latest  ← 漏判（云元数据）
0 http://169.254.169.254#@example.com/latest  ← 漏判
0 http://10.0.0.5#@public.example.com/x   ← 漏判
0 http://[::1]#@example.com/x             ← 漏判
0 http://a@b@127.0.0.1/                   ← 漏判（第一个 @ 被当 userinfo）

$ ./red_fixed             # 本轨修复版，同一 main()
1 …（9 行全部为 1）
```

**GREEN（进程内）**：`tests/test_r22_data_security_consistency.cpp` 两个 TEST_CASE（`isPrivateHost terminates the authority…`、`resolveForPinning judges IP literals…`）全绿；既有 `test_io_remote_validator` 的 Oracle-B2.1 全量不回归。

## #1700（P1 RangeDiskBlockStore blockSize 键）

**RED（两层证据，R1 复核后修正）**：
1. **basis 格式断言（决定性）**：master `identityBasis`（range_cache_disk.cpp@911e0b17:298-315）返回 `"etag\n"+url+"\n"+etag`，不含 blockSize；新用例断言 `bs=65536`/`bs=131072` 两键不同——master 上该断言直接失败（旧格式串无 `bs=` 段）。
2. **store 层 oracle（R1-F1 新增）**：`RangeDiskBlockStore keys blocks by geometry…` 用例在同一目录、同一 URL+ETag 下以 64K basis `putBlock(index 1)`，随后以 128K basis `readBlock(index 1)`——master 上两 basis 相同 → 命中并返回 [64K,128K) 字节（`CHECK_FALSE` 失败）；修复后键分离 → MISS。

**修正声明（R1 指出）**：此前版本此处声称「集成测试第二段在 master 上必然失败（loadBlocksFromDisk 命中旧块）」——**不成立**：`InstalledCache` 析构触发 `uninstall()`，后者无条件清空磁盘层，集成测试的 pass 2/3 实际重新拉取 fixture、在 master 上也会通过（vacuous）。集成用例保留为端到端合同钉子（已加注释说明），决定性证据由上述 store 层 oracle 承担。

**GREEN**：basis 含 `bs=<blockSize>`（两键不同 + store 层 MISS）；geometry 变化同时 `RangeDiskBlockStore::clear()`；`loadBlocksFromDisk` 拒绝 `data.size() > blockSize` 的块。R1-F2 后 updateConfig 的比较/应用合并到单个锁域。

## #1701（P2 atomic_fs 崩溃一致性）

**RED（逐行矛盾）**：master atomic_fs.cpp@911e0b17:767/875 `renameReplaceQuiet(journalPath, commitMarkerPath)` 返回值丢弃 → 新用例的 `REQUIRE(refused)` 在 master 上失败（无异常路径可达）；`writeJournalFile`（:392-397）无 `out.good()` 检查 → 磁盘满截断日志被 recover 当无效删除；journal 写前不清 `.bak` → 「stale .bak」用例在 master 上 cleanup() 会把 ANCIENT 字节恢复到 target（`readFile(target)=="CURRENT"` 断言失败）。

**GREEN（真实机制注入，无测试专用钩子）**：commit-marker 失败用「非空目录占据 marker 路径」（POSIX rename(file,dir)=EISDIR）注入；sidecar 失败用「目录占据 sidecar 目标」注入。三用例全绿：失败后 journal+.bak 保留、recover 完整回滚、主目标不被陈旧字节覆盖。

## #1680（P2 ArtifactObjectPool 并发发布）

**RED（逐行矛盾；R2 措辞修正）**：master artifact_object_pool.cpp@911e0b17:176-180 rename 失败分支直接 `return std::nullopt`，不复查 objectPath；tmp 名 `.<pid>.puttmp`（:143-145）同进程两实例同目标互踩。双实例×双线程×8 轮（start gate 后）的 `successes==2` 在 master 上**大概率**失败（败者 rename 撞赢者对象 → nullopt；或 tmp 被对方 remove → copy 后 rename 失败）——并发时序使然，非确定性构造；修复后为确定性通过（R2 连续 10/10）。R2 并实证了本工具链上 Qt rename 对已存在目标返回 false 且不动目标（#1680 前提成立）。

**GREEN**：tmp 名加进程内原子计数后缀；rename 失败且 objectPath 已存在 → `continue` 重走顶部 digest 验证。8/8 轮双成功。

## #1702（P2 SamplePromoter provenance）

**RED（逐行矛盾）**：master sample_promotion.cpp@911e0b17:264/367/500/589 四处丢弃 `Result<void>`。用例以 SQLite 直写把 draft 行 manifest_json 损坏（`requireDraftVersion` 只读 status，入口不拦；`addSourceAssetsToDraft` 内 `DatasetManifest::fromJson` 失败）→ master 上 promotion 仍返回 success（断言 `!broken.has_value()` 失败）。

**GREEN**：链接失败 → `dataset.promotion_partial` + 原诊断，promotion 失败如实上报；`[[nodiscard]]` 防回归。

## #1703（P3 清理）

- ① **RED**：master raster_reader.cpp:214 `window.xOff + window.width`（int）在 {1, INT_MAX} 处回绕为负 → 校验通过；新用例 `CHECK_FALSE(validateWindow(...))` 在 master 失败。**GREEN**：int64 比较 + 边界/正常窗用例。
- ② readMask：**证据修正**——issue 称「无调用者」，实测 tests/ 有 6 处调用（test_io_raster_contract ×5、test_io_fidelity ×1），语义被测试钉死。处置：头文件注明「不咨询 GDAL mask/alpha band」的范围限制，不改语义不删 API（详见 CROSS_TRACK_FINDINGS）。
- ③ **RED**：master dataset_store.cpp:728 等 4 处 `stepRow()` 失败折进 `dataset.not_found`。**GREEN**：`stepFailed()`（既有原语，#1448/#1457 同契约）区分 `dataset.store_query_failed`。4 处同型全部修复（addSourceAssetsToDraft/stageVersion/commitVersionImpl/deprecateVersion）。

## #1679 扩展对抗输入（修复版，2026-10-26 补充）

对修复版额外跑了 16 个对抗形态（`/tmp/r22red/red_extra.cpp`）：IPv6+端口+query、zone-id、0.0.0.0/组播/尾点字面量、大写、空 authority、scheme-in-path、编码 `%40`、反斜杠。14/16 与「curl 实际连接目标」判定一致。两个非匹配经分析为**期望过严而非缺陷**：

1. `http://127.0.0.1\@example.com/` → gate 判 example.com（公网）。curl 的 urlapi 仅以 `/?#` 终止 authority，`\` 不是终止符，userinfo=`127.0.0.1\`、host=`example.com` —— gate 与 curl 判定**一致**（WHATWG 浏览器语义把 `\` 当 `/`，但本 gate 只服务 libcurl/GDAL VSI）。无绕过。
2. `http://10.0.0.1%40.example.com/` → 两侧 host 均为含编码字符的不可解析名，curl 同样无法连私网。无绕过。

结论：修复后 gate 的 authority 语义与 libcurl 一致；判「公网」但 curl 连私网的分歧面已闭合。
