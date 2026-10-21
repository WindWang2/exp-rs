# R15-B Oracles

全部位于 `tests/test_r15_B_geodata_persistence.cpp`（ctest 前缀 `test_r15_B_geodata_persistence::`）。

| ID | 用例名（前缀省略） | 不变量 | 分类 |
|---|---|---|---|
| O-B13.1 | `B13: RasterReader refuses a group left mid-publish by a crash` | 存在未提交 journal（侧车已换新、主文件仍旧）时，读者以 `IoError`+"interrupted mid-publish" 拒绝；拒绝无副作用（journal、.bak 保留）；`recoverDatasetGroup` 后恢复完整旧组并可读 | newly-reproduced |
| O-B13.2 | `B13: a committed marker means the new group is complete and readable` | committed 标记（仅 .bak 清扫被中断）不阻断读取 | 防误杀 |
| O-B13.3 | `B13: VectorReader refuses a shapefile whose .dbf is from another generation` | .shp 旧 + .dbf 新（属性/几何错配）不被读出 | newly-reproduced |
| O-B13.4 | `B13: kill -9 at random points of group publish never leaves a mixed generation` | 固定种子 1505，40 轮 fork+SIGKILL 子进程循环发布 3 成员组；每轮恢复后 main/.aux.xml/.tfw 同代；未提交窗口内读侧闸门生效（POSIX only） | 验证既有恢复 + 新闸门 |
| O-B05.1 | `B05: CatalogService::searchAll detects a repeating rel=next instead of duplicating` | 远端 next 自环 ⇒ `InvalidMetadata`，≤3 次请求，无重复记录 | newly-reproduced |
| O-B05.2 | `B05: distinct pages still paginate to completion with exact counts` | 正常 3 页=5 条；maxItems=3 ⇒ 3 条且 truncatedByCap | 防误杀 |
| O-B04.1 | `B04: remote identity resolver answers a refused origin with an empty token` | 私网源 ⇒ 不抛异常、空 token、0 次出站请求 | regression-of-existing-fix（#1504） |
| O-B12.1 | `B12: bundle import never relocates symlinked or checksum-uncovered data/ payloads` | data/ 下符号链接（指向包外秘密文件）与未列入 checksums.txt 的文件不迁移且有告警；被列入并哈希校验的文件仍迁移 | newly-reproduced（#1481 T12 残留） |
| O-B07.1 | `B07: ExperimentStore list scans report a mid-scan SQLITE_CORRUPT as failure` | 真实损坏一个表叶页后 listRuns 只能"完整成功"或"类型化失败"，不得返回部分结果为成功 | newly-reproduced（#1448/#1457 残留） |
| O-B16.1 | `B16: #1481 SSRF host-classification threads stay closed on master` | #1481 T1/T3/T4/T13/T16、#1505 T4、#1485 字面量 | candidate regression oracle（FIXED_ON_MASTER） |
| O-B16.2 | `B16: #1481 T17 direct COG validation is gated before GDAL dispatch when offline` | offline 时 validateCog 在 GDAL 之前拒绝 | candidate regression oracle |

另：`test_io_remote_validator::the session identity resolver returns on first sight, never deadlocks` 的期望由强 ETag 改为空 token（回环被 #1504 默认阻断），首见路径仍会在 probe 之后获取发布锁，死锁回归仍被覆盖。

追加（`7d2ac030` / `e89b9b51`）：

| ID | 用例名（前缀省略） | 不变量 | 分类 |
|---|---|---|---|
| O-B02.1 | `B02: full read equals tile-walk and block stitches for integer compressed rotated rasters` | 整型（Byte/Int16/UInt16/Int32/UInt32，含极值与 nodata）× 2 波段、37×23 非整块尺寸、16×16 DEFLATE 瓦片、旋转/剪切地理变换下，整幅读取 == 分块遍历拼接 == block 拼接，逐像素相等 | candidate regression oracle（首跑即绿，未发现缺陷） |
| O-B16.3 | `B16: #1485 octal and trailing-dot loopback spellings are private` | `0177.0.0.1`（八进制）、`localhost.`（尾点）等拼写被判定为私网 | candidate regression oracle（FIXED_ON_MASTER） |
| O-B14 | `B14: bulk catalog paging and experiment-store throughput (measurement)` | 隐藏用例 `[.]`，只做测量，不做断言门禁；数字见 PERFORMANCE.md | measurement |
