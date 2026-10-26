# fix(r22-data): 空间数据访问安全与资产一致性 — SSRF authority 解析、远程 COG 磁盘缓存几何键、原子发布崩溃一致性、并发发布收敛与数据集 provenance 闭环（#1679 #1700 #1701 #1680 #1702 #1703）

## Base / Head

- base：`master` @ `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`
- head：`agent/r22-data-20261025T132358Z` @ `eb24ccb4dd9d3dc75f1fed9724fcca7e291bba78`（7 commits）

## 关闭 / 关联

- Closes #1679（P0 SSRF authority 解析绕过）
- Closes #1700（P1 RangeDiskBlockStore blockSize 键缺失）
- Closes #1701（P2 atomic_fs 发布日志崩溃一致性）
- Closes #1680（P2 ArtifactObjectPool 并发发布同 digest）
- Closes #1702（P2 SamplePromoter provenance 静默丢失）
- Closes #1703（P3 data 线清理清单：①③ 修复，② 以证据修正处置并注明范围）
- 延续 #1527（安全下载防线，不回滚，只堵其绕过面）

## 改动清单（全部在本轨授权 write-set 内）

- `src/geospatial/remote/offline_gate.{cpp,h}`：#1679 authority 解析统一（先 `/?#` 截断、后 `rfind('@')`，与 libcurl/RFC 3986/urlHostOf 一致）；DNS 分支复用 `isPrivateIpv6`（补 `::`/multicast/NAT64/IPv4-compatible）；`resolveForPinning` 在进程策略下自行拒绝私网 IP 字面量（纵深，fail-closed）。
- `src/geospatial/remote/range_cache{,_disk}.{cpp,h}`：#1700 `identityBasis` 携带 `bs=<blockSize>`；几何变化时内存+磁盘层同时失效（比较/应用同一锁域）；`loadBlocksFromDisk` 拒绝超长块。
- `src/geospatial/util/atomic_fs.cpp`：#1701 提交标记 rename 检查（失败保留 journal+.bak 并抛错）；journal 写后 `good()` 校验；陈旧 `.bak` 复活或清扫（target 缺失时 `.bak` 是唯一副本 → 复活，绝不删除——R2 确认此为 R1 轮引入缺陷的修复）。
- `src/data/artifact_object_pool.cpp`：#1680 rename 失败且对象已存在 → 重走 digest 验证收敛；tmp 名 pid+进程内原子计数（每次调用唯一）。
- `src/dataset/sample_promotion.cpp`、`dataset_store.{cpp,h}`：#1702 四处 `addSourceAssetsToDraft` 全检查 + `dataset.promotion_partial` 如实诊断 + `[[nodiscard]]`；#1703-③ `stepFailed()` 与 `not_found` 区分（4 处同型）。
- `src/geospatial/raster/raster_reader.{cpp,h}`：#1703-① validateWindow int64 化；readMask 范围注明（issue「无调用者」前提被 6 处测试调用推翻，保守不改语义）。
- 测试：新增 `tests/test_r22_data_security_consistency.cpp`（9 用例）；`test_io_range_cache.cpp` 追加 store 层几何键 oracle + 集成用例（含诚实注释）；`tests/CMakeLists.txt` append-only 注册。
- 证据：`.planning/r22-data/`（BASELINE/OWNERSHIP/PR_COLLISION_MATRIX/ISSUE_TRIAGE/TASKS+ORACLES+RISK/LOOP_LEDGER/RED_GREEN/PERF_BASELINE/TEST_RESULTS/REVIEW_R1/REVIEW_R2/FINAL_REPORT）。

## RED → GREEN 证据（摘要，全文见 RED_GREEN.md）

- #1679：独立编译 master blob 运行 `http://127.0.0.1?@example.com/` 等 9 形态 → master 7 漏判（判公网），修复 9/9 拒绝；另 16 个扩展对抗形态 curl-parity 全分析（2 个非匹配经论证为期望过严，gate 与连接器判定一致）。
- #1700：master 上同 URL+ETag 两几何同键 → store 层用例 `CHECK_FALSE(readBlock(basis128,1))` 失败（命中旧字节）；修复后键分离 MISS。basis 格式断言 `bs=65536/131072` 为编译独立证明。（R1 指出初版集成测试 vacuous——已改 store 层 oracle 并纠错文档。）
- #1701：真实机制注入（目录占位 marker 路径 → POSIX rename EISDIR；sidecar 目标占位）：master 上 marker 失败被忽略 + 删 .bak（假成功+半销毁）、陈旧 ANCIENT 字节回滚覆盖、孤儿唯一副本被删——修复后全部失败抛错且恢复材料完整。
- #1680：start-gate 双线程 × 8 轮 ×（R2 再 10 连跑）全收敛；Qt rename 对已存在目标失败且不动目标已实证。
- #1702：SQLite 直写损坏 manifest 注入 → master 静默成功（断言失败），修复后 promotion_partial 如实上报。
- #1703-①：`{xOff=1, width=INT_MAX}` master 通过校验（int 回绕），修复后拒绝；正常窗 2094 断言契约族全绿。

## 双轮独立 Review

- **R1**（分离上下文 subagent）：**APPROVE**；8 findings 全闭环（F1 P2 vacuous 测试已重写为 store 层 oracle、F2 锁域折叠、F5/F6 测试强化）。
- **R2**（独立 subagent，不采信 R1）：**APPROVE** @ 5fc544a6c6；确认 R1 修复轮曾引入 P1 数据丢失（孤儿 .bak 唯一副本被清扫删除）——该缺陷在 R2 报告前已由本轨主动发现并修复（复活语义 + 决定性测试）；R2-2/3/6 follow-up 闭环，R2-4/5/7/8 如实记录（理由见 REVIEW_R2.md）。

## 测试与双跑

15 个测试族全绿（新套件 94 断言/9 用例；SSRF 族 232/27；range_cache 327/39；geodata_persistence 8305/20 等，全表见 TEST_RESULTS.md）；关键安全/崩溃恢复/数据一致性族双跑（部分三跑 + R2 独立 10 连跑）。**不等待线上 CI**（本地可执行检查为准；无 GPU/TSan/Windows/macOS 环境，未执行项已在 TEST_RESULTS 声明）。

## 性能

N/A（PERF_BASELINE.md 逐项分析：无热路径新增循环/锁/分配；range_cache 遥测预算断言同时充当回归哨兵）。

## 与 R21/R22 兄弟轨差异核对（push 前实时复核）

- origin/master 未动（`911e0b17`）。
- #1673（r21-B，open）：唯一 src 相邻点 `src/dataset/dataset_store.cpp`——本轨只改 `addSourceAssetsToDraft`/`stageVersion`/`commitVersionImpl`/`deprecateVersion` 的 stepRow 错误分支，#1673 改 createDataset TOCTOU，hunk 不重叠，语义正交；不采信、不覆盖其未合并修复。
- #1708（r22-infra）/ #1709（r22-inference）：与本轨 src 文件零交集；`tests/CMakeLists.txt` 双方 append-only 相邻追加（Git 可自动合并，与合并顺序无关）。
- 本轨不回滚 #1527/#1669/#1667 的任何防线（artifact_object_pool 自愈、SSRF gate 均为增量加固）。

## 遗留风险与未执行项

- `isPrivateHost` 与 `urlHostOf` 仍为两份解析器（percent-decode 语义差异，已论证所有分歧方向 fail-closed）；统一为单一解析器列为后续清理。
- #1701 跨进程并发 publish 同一目标仍无进程间锁（master 既有，本轨未扩大也未解决）。
- #1700 几何切换 clear() 顺带丢弃同几何可用块（冷启动回填代价，注释已注明）。
- B3/B5/B6 里程碑与 #1587/#1548/#1545 候选：NOT_STARTED（FINAL_REPORT/CROSS_TRACK_FINDINGS 有记录）。
