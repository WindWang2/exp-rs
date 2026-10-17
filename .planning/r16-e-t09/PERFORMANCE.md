# R16-E T09 — Performance Record（WP4 搜索索引）

测量对象：`tests/test_r16_E_help_search_perf`（固定种子 mt19937(20261017) 合成集：5000 描述符，
CJK+EN 混合 title/summary/keywords；查询含命中全体语料的重 token —— 每个 summary 都含
"辐射/斑点/terrain"，故单查询 posting 命中可达 ~5000 文档）。

宿主：40 核 / 62GB；**Debug 构建**（CMAKE_BUILD_TYPE=Debug，无优化；Release 数字应显著更低，
本轨道未配 Release，如实注明）。本机无 `/usr/bin/time`，RSS 由 Python
`resource.getrusage(RUSAGE_CHILDREN).ru_maxrss` 外测（等价于 time -v 的 max resident）。

命令：`QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/home/kevin/wp2-prefix/usr/lib
python3 <wrapper> ./build-r16-e-t09/test_r16_E_help_search_perf`（wrapper 打印 stderr 的
`[wp4]` 行 + ru_maxrss）。

## 实测（优化阶梯，每步真实运行）

| 步骤 | 3000 查询 | 508×3 查询 | build×2 | tokenize 1MiB CJK | 峰值 RSS | 判定 |
|---|---|---|---|---|---|---|
| master 基线（全量 sort+截断，QString 键 score join） | **73367 ms** | 393 ms（仅 8 稀有查询计时） | 441/445 ms | 726 ms | 138776 KB | FAIL（>15000 ms 上限，exit 42） |
| + top-K `nth_element`（同全序，集合唯一） | 36093 ms | 217 ms | 430 ms | 73367→— | ~130 MB | FAIL |
| + (score,desc*) 对排序、仅物化前 K 个 SearchHit | 38382 ms | 215 ms | 448 ms | — | 130016 KB | FAIL（无增益，噪声内） |
| + **稠密 int 文档下标 posting**（`QHash<QString,QHash<int,double>>`；score 累积走 `QVector<double>`，无逐条 QString 哈希） | **11273 ms** | 61 ms | 394/398 ms | 726 ms | 123500 KB | **PASS** |
| 同上，第二次运行（GREEN ×2） | **10802 ms** | 59 ms | 369/375 ms | 709 ms | 122424 KB | **PASS** |

结论：热点不在排序而在 **score join 的逐 posting 条目 QString 哈希查找**（~20k 次/查询，
Debug 下 ~12ms/查询）。稠密下标化后 3000 查询 **73.4s → 11.3s（6.5×）**，RSS 降 11%
（138.8MB → 123.5MB），并让测试原定的 15000ms 上限成立（无需放宽 Oracle）。
真实语料仅 ~700 页，单查询本就 ~1-2ms 级 —— 该优化对交互式搜索框按键查询是裕量改善，
非用户可见缺陷修复；如实记录，不夸大。

## 评分确定性判定

同一输入两次独立 build 的索引 + 同索引重复查询，(id + 17 位有效数字 score) 序列**逐字节一致**
（测试内 `run1==run2==other` 断言，两次进程运行 All tests passed）。原理保持：最终排序键
(score 降序, id 升序) 且 id 唯一 → 全序；每文档得分累积按查询 token 顺序；稠密下标化只改
键的表示（QString→int），不改变任何加法次序或比较语义；重复 id 输入沿用旧合并语义
（同槽累积、后到者覆盖显示字段）。

## tokenize / build 基线（无缺陷，记录）

- build 5000 描述符（Debug）：369-454 ms（上限断言 30000 ms）
- tokenize 1MiB 变体 CJK（15000 字符循环表）：709-733 ms → 15000 tokens（上限 10000 ms）
- 512KiB 变体 CJK（WP3 测试内）：<2000 ms 预算内通过

## 发现但未修

- 无其它真实低效。`search()` 每查询对 query tokenize 一次（无重复切分）；`tokenize()` 的
  `removeDuplicates()` 为 O(n²) 但 n=查询 token 数（≤10 级），不构成热点；未做无依据微优化。
