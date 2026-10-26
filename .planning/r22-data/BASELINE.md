# R22-02 (r22-data) BASELINE — 空间数据访问安全、远程 COG 缓存、资产一致性与可复现数据血缘

## 执行时点事实（2026-10-25 13:2x–13:4x UTC 现场采集）

- **远端 master**：`gh api repos/WindWang2/exp-rs/commits/master` → `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（2026-10-23T01:17:48Z），与任务锚点一致。
- **本地 origin/master** = `911e0b17`（`git fetch` 经 HTTPS 间歇 SSL/连接失败，按预案以 `gh api` 核实远端 SHA 后确认本地 ref 未落后）。
- 本地主工作树 master = `28e1891390`，behind 59、clean、**未触碰**；`git merge-base --is-ancestor 28e1891390 origin/master` ✅（祖先关系正常，无分叉）。
- **viewerPermission = ADMIN**；default branch = master。
- **Open PR = 5**（全部为 R21 轨，无 R22 轨）：#1673 (r21-B data/model-runtime)、#1674 (r21-E)、#1675 (r21-C)、#1676 (r21-A)、#1677 (r21-D)。head 均为 2026-10-23/24 推送，未见活跃更新（最后 updatedAt 2026-10-24T14:43Z）。
- **Open Issues = 156**。
- 本轨 worktree：`/home/kevin/project/exp-rs-r22-data-20261025T132358Z`，branch `agent/r22-data-20261025T132358Z`，base = origin/master@911e0b17（唯一 worktree、唯一分支，创建前核对无同名）。

## 环境

- 主机 cachyos x86_64，40 核 / 62GB（free -h：46GB available）；磁盘 343G 可用。
- 跨轨构建锁：按任务规定 `/tmp/exp-rs-r21-build.lock`，并加取 R19–R21 实际使用的 `/home/kevin/project/exp-rs-build.lock`（双锁串行，绝不并发编译）。`-j4` 硬上限。
- vendor 前缀（沿 R19-01/R21-B 配方）：`/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr`；Qt6/OpenCV 系统；CMake Unix Makefiles / Release / ENABLE_TESTS=ON。
- build dir：`build-r22-data`（自 R21-B 同基底 911e0b17 的构建缓存复制后改写路径为本轨私有，绝不与他人共用）。
- `gh` CLI 可用；git https 间歇失败（fetch 重试 + api 校验为兜底）。

## 第一批 Issue 源码级验真（@911e0b17，全部 OPEN_UNFIXED）

| Issue | 分级 | 状态 | 源码证据（行号为现场读取） |
|---|---|---|---|
| #1679 SSRF authority 解析 | P0 | **OPEN_UNFIXED** | `offline_gate.cpp:517-521`：`atPos=afterScheme.find('@')`（第一个 @），仅与 `firstSlash` 比较；随后才 `find_first_of("/?#")` 截断 —— `?`/`#` 之后、`/` 之前的 `@` 会被当 userinfo 剥掉，`http://127.0.0.1?@example.com/` 解析出 `example.com`。`urlHostOf`(http_fetch.cpp:469-487) 先以 `/?#` 截断 authority 再 `rfind('@')`，与 curl/RFC3986 一致 —— 两解析器不一致即绕过：gate 判公网，pinning 对 `127.0.0.1` 字面量 `NoResolutionNeeded`(offline_gate.cpp:666-673)。DNS 分支(offline_gate.cpp:601-631)手写 IPv6 判定缺 `::`/multicast/NAT64/IPv4-compatible，与 `isPrivateIpv6()` 规则分叉。 |
| #1700 RangeDiskBlockStore blockSize 键 | P1 | **OPEN_UNFIXED** | `range_cache_disk.cpp:298-315`：`identityBasis` 只含 URL+strong ETag，**不含 blockSize**；块文件名 `<basisHash>.<blockIndex>.blk`(:67-71)；块序列化/校验头(:74-135) 不记录 blockSize；`range_cache.cpp:869-876` `updateConfig` 在 geometryChanged 时仅 `dropAllLocked()`（内存），磁盘层保留；`loadBlocksFromDisk`(:748-786) 不校验 `data.size()` 与 blockSize 关系；`tryServe`(:643-695) 以当前 blockSize 定位。 |
| #1701 atomic_fs 崩溃一致性 | P2 | **OPEN_UNFIXED** | `atomic_fs.cpp:767` 与 `:875`：`renameReplaceQuiet(journalPath, commitMarkerPath)` 返回值被忽略，随后删 `.bak`+marker → 重命名失败时残留未提交日志，recover(isCommitted=false) 对 `had_target=false` 成员 `removeFileQuiet(target)`(:452-460) 删掉**已发布成功**的数据；`writeJournalFile`(:385-399) 写后未查 `out.good()`/close；日志写前未清理陈旧 `.bak`（recover 解析失败路径只删日志不删 .bak，:419-421 → 陈旧 .bak 可在后续 run 的 journal-写后/backup 前 崩窗内被当回滚源）。 |
| #1702 SamplePromoter provenance | P2 | **OPEN_UNFIXED** | `sample_promotion.cpp:264/367/500/589` 四处 `m_store->addSourceAssetsToDraft(...)` 丢弃 `Result<void>`；`dataset_store.cpp:707-760` 该函数可因 store 关闭/只读/未找到/非 draft/manifest 解析失败/SQL 失败而失败；`dataset_store.h:98` 无 `[[nodiscard]]`。 |
| #1680 ArtifactObjectPool 并发发布 | P2 | **OPEN_UNFIXED** | `artifact_object_pool.cpp:176-180`：`!QFile::rename(tmp, objectPath)` → 删 tmp、`return std::nullopt`，未处理「目标已存在且同 digest」；tmp 名 `.<pid>.puttmp`(:143-145) 同进程多实例/线程互踩。 |
| #1703 data 债务清单 | P3 | **OPEN_UNFIXED** | ① `raster_reader.cpp:213-214` `window.xOff + window.width` int 溢出 UB；② `readMask`(:322) 忽略 GDAL mask/alpha band 且 raster_reader 外无调用者（grep src/ 无外部引用）；③ `dataset_store.cpp:727-733` `stepRow()` 失败被报为 `dataset.not_found`。 |

**排重证明**：6 个 issue 的目标文件与 5 个 open PR（#1673–#1677）的变更文件交集为空（#1673 改 `src/data/artifact_store.cpp`、`workspace_catalog.cpp`、`src/dataset/dataset_store.cpp`、`src/experiment/*` 等 —— 本轨触碰的 `dataset_store.cpp` 仅在 #1703-③ 修复（改 `addSourceAssetsToDraft` 的误报错误码）时相交，见 PR_COLLISION_MATRIX；`sample_promotion.cpp`/`artifact_object_pool.cpp`/`atomic_fs.cpp`/`geospatial/remote/*` 均无 open PR 触碰）。`tests/CMakeLists.txt` 为允许的 append-only 跨轨注册表。

## R21-B (#1673) 同域重叠面（重点）

#1673 的 write-set 含 `src/dataset/dataset_store.cpp`（A6 createDataset TOCTOU 误码修复）。本轨 #1703-③ 同样触碰 `dataset_store.cpp`（`addSourceAssetsToDraft` 的 stepRow 误报）——不同函数、不同行域，语义无冲突；合并时以 hunk 级独立共存，PR body 将说明。本轨不改 #1673 所改的其他文件。
