# R22-02 (r22-data) PERF_BASELINE

**结论：N/A（无性能改动需要基准对比）。**

逐项说明（改动点的性能影响均为非热路径或一次性）：

| 改动 | 热点? | 影响分析 |
|---|---|---|
| #1679 isPrivateHost authority 解析重排 | 每次远端 gate 判定（每 URL 一次） | 字符串操作次数同量级（find_first_of + rfind 替代 find+find+find_first_of）；无循环/分配增加。独立 RED 程序两版对照运行无可测差异（微秒级函数）。 |
| #1679 DNS 分支 isPrivateIpv6 统一 | 仅 DNS 名解析路径 | 分支更少（一次函数调用替代三段手写判定）。 |
| #1679 resolveForPinning 字面量判定 | guarded transport 每 hop 一次 | 两个 if + 位运算，纳秒级。 |
| #1700 identityBasis 追加 `\nbs=<n>` | 每资源每次 diskBasis 计算 | 每块 IO 伴随一次字符串拼接；相对 SHA-256 块哈希（主开销）与磁盘 IO 可忽略。 |
| #1700 updateConfig 几何变化时 RangeDiskBlockStore::clear() | 仅 blockSize 变更时（配置操作） | 一次性目录清扫（原有 kEvictionScanEntryCap 上限机制）；正确性要求（陈旧布局字节不再占配额）。 |
| #1700 loadBlocksFromDisk size 校验 | 每磁盘块装载 | 一次比较。 |
| #1701 journal good() 检查 / .bak 清扫 / marker 检查 | 每次 group publish | 每次 publish 增加常数次 stat/remove 调用；publish 本身是重 IO 操作（多文件 fsync+rename），相对可忽略。 |
| #1680 tmp 名原子计数后缀 | 每次 put staging | 一次 atomic fetch_add。 |
| #1680 rename 失败 continue 重走 | 仅并发撞名时 | 罕见路径；重走顶部 digest 校验是有意为之（自愈语义）。 |
| #1702 四处 Result 检查 | 每次 promotion | 零成本（本就要构造返回值）。 |
| #1703-① int64 比较 | 每次 validateWindow | int64 加法与比较，等价于 int 版本。 |
| #1703-③ stepFailed() 分支 | 每次版本行读取 | 一次 int 比较。 |

无新增循环、锁、分配于任何热路径；`test_io_range_cache` 的吞吐/遥测断言（bytes_fetched 预算类）在 GREEN 运行中同时充当回归性能哨兵。
