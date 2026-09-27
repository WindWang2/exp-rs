# Portability Coverage Report — WP-A (core-foundations-r4)

基线 `15e5c66b5`;复核命令可复跑(每节标注 rg 命令)。收口判据(可证伪):**src/core 内白名单外平台 API 直调 = 0,或全部有据豁免** → 本报告主张后者(全部 13 个残留点均有据豁免,见 §3)。

## 1. Helper × 模块采用矩阵(正向证据,`rg -l "sicnu::portable::<helper>" src --glob '!src/platform/portable.h'`)

11 个 helper(portable.h 实测;提示词记 6 个为过时值):

| helper | 采用文件数 | 模块分布(文件数) | 采用状态 |
|---|---|---|---|
| `pid` | 14 | geospatial:4 sdk:3 runtime:2 workflow:1 lab:1 data:1 cli:1 agent_loop:1 | 广泛采用(替代每文件 `#ifdef GetCurrentProcessId/_getpid` 垫片;src/core 非 vendor 面垫片存量 = 0,rg 证据 §3) |
| `pathFromUtf8` | 31 | geospatial:16 sdk:5 runtime:4 recipes:2 plugins:1 lab:1 agent_loop:1 agent:1 | 广泛采用(UTF-8 约定的主边界) |
| `pathToUtf8` | 10 | geospatial:4 runtime:3 lab:1 agent_loop:1 | 采用中 |
| `envUtf8` | 11 | sdk:3 recipes:2 runtime:1 lab:1 geospatial:1 data:1 app:1 agent:1 | 广泛采用(source pin: workspace root / session dir / curriculum 等安全输入已锁) |
| `wideFromUtf8`(WIN32) | 2 | geospatial:1 lab:1 | Windows 面采用;POSIX 编译单元按 `#if defined(_WIN32)` 不可见 |
| `utf8FromWide`(WIN32) | 1 | geospatial:1 | 同上 |
| `isWindowsReservedName` | **0** | — | **零调用方**。portable.h:146-150 声明其用途为 "validation seams ... on Windows-bound writes";当前仓库无该 seam,helper 处于"先行+unit-tested 待采用"状态(test_platform_portability "classifies device names" 用例守护)。处置:保留,不强行制造调用点(禁止为收敛而收敛) |
| `claimExclusiveUtf8` | 1 | agent_loop:1(session_journal staging claim) | 采用中;atomic_fs.cpp `stagedPathFor` 同语义手写(CreateFileW/O_EXCL)→ §4 重复面发现 |
| `fileOpenUtf8` | 1 | geospatial:1(range_cache_disk;source pin 锁定) | 采用中 |
| `syncFileUtf8` | 1 | agent_loop:1(journal durability gate) | 采用中 |
| `syncDirectoryBestEffortUtf8` | 1 | agent_loop:1(journal publish 后目录 fsync) | 采用中 |

新测试覆盖(本轨道):`claimExclusiveUtf8` 此前无任何 TEST_CASE(全仓 rg 证实)→ 已在 test_platform_portability.cpp 补齐独占语义用例(首 claim 独占/同 claim 拒绝/O_EXCL 不截断/移除后可复用);`syncDirectoryBestEffortUtf8` 已由既有 syncFileUtf8 用例驱动(合法目录 + 缺失目录静默,test_platform_portability.cpp:229-232),无需重复。

## 2. 采用缺口结论

- 采用面呈"核心边界 + 新 helper 低扩散"形态:`pathFromUtf8`/`envUtf8`/`pid` 已成默认入口;5 个 2026-09 新 helper 各 0-1 调用方,符合其"新发布、待自然扩散"状态。
- 不存在"该收敛而未收敛"的存量:src/core 非 vendor 文件七轴普查全部为 0(见 §3 表)。

## 3. 平台 API 残留逐点处置清单(全部豁免,依据 DECISIONS D-2)

### 3.1 七轴零散点普查(2026-09-27,worktree 实测;`rg -n … src/core -g '!*qgs*'` 各返回空)

| 轴 | 模式 | 非 vendor 命中 |
|---|---|---|
| pid 垫片 | `GetCurrentProcessId\|_getpid\|getpid(` | **0** |
| 环境读取 | `GetEnvironmentVariable\|\bgetenv\b` | **0** |
| 窄 fopen | `_wfopen\|std::fopen\|fopen(` | **0** |
| 宽窄转换 | `MultiByteToWideChar\|WideCharToMultiByte` | **0** |
| 裸 rename | `fs::rename\|std::filesystem::rename` | **0** |
| fsync | `fsync\|FlushFileBuffers` | **0** |
| Win 移动/替换 | `MoveFile\|ReplaceFile` | **0** |

### 3.2 残留点(13 个,全部位于 QGIS upstream vendor 移植层)

| # | 文件 | 残留 | 计数 | 处置 | 理由(可复核) |
|---|---|---|---|---|---|
| 1 | `src/core/qgsfileutils.cpp:340` | CreateFileW | 1 | **豁免** | Windows 长路径/目录探测(`OPEN_EXISTING+FILE_FLAG_BACKUP_SEMANTICS` 打开目录句柄),与 portable.h 的路径编码边界无关;helper 无对应能力;QGIS upstream 代码 |
| 2 | `src/core/providers/gdal/qgsgdalprovider.cpp` | GetLastError | 20 | **豁免** | GDAL/OGR provider 错误轮询(QGIS upstream);错误源是 vendor 驱动回调,非路径/编码边界 |
| 3 | `src/core/providers/ogr/qgsogrprovider.cpp` | GetLastError | 21 | **豁免** | 同 2 |
| 4 | `src/core/providers/ogr/qgsogrproviderconnection.cpp` | GetLastError | 8 | **豁免** | 同 2 |
| 5 | `src/core/providers/ogr/qgsogrproviderutils.cpp` | GetLastError | 6 | **豁免** | 同 2 |
| 6 | `src/core/providers/ogr/qgsogrprovidermetadata.cpp` | GetLastError | 5 | **豁免** | 同 2 |
| 7 | `src/core/qgsvectorfilewriter.cpp` | GetLastError | 11 | **豁免** | OGR 写驱动错误面(QGIS upstream) |
| 8 | `src/core/qgsofflineediting.cpp` | GetLastError | 5 | **豁免** | 同 2 |
| 9 | `src/core/providers/ogr/qgsgeopackageproviderconnection.cpp` | GetLastError | 2 | **豁免** | 同 2 |
| 10 | `src/core/providers/ogr/qgsogrtransaction.cpp` | GetLastError | 1 | **豁免** | 同 2 |
| 11 | `src/core/providers/ogr/qgsgeopackageprojectstorage.cpp` | GetLastError | 1 | **豁免** | 同 2 |
| 12 | `src/core/qgsarrowiterator.cpp` | GetLastError | 1 | **豁免** | 同 2 |
| 13 | `src/core/browser/qgsfilebaseddataitemprovider.cpp` | GetLastError | 1 | **豁免** | 同 2 |

合计:CreateFileW 1 处 + GetLastError 93 处/12 文件 = **全部有据豁免**,零"改不动"式留白。`_wstat`/`WideCharToMultiByte`/`LoadLibraryW` 在 src/core = 0(复核通过)。
统一豁免依据(D-2):QGIS upstream vendor 边界,逐点改造制造长期 merge drift;与 portable.h 头注对既有本地变体("stay put on purpose")的既有处理先例一致。vendor 面不适用 helper 收敛(调用点在驱动错误回调内,非编码边界)。

## 4. 重复面发现(记录,defer-#1338)

`src/geospatial/util/atomic_fs.cpp` 与 portable.h 存在两组同语义重复:`stagedPathFor` 手写 O_EXCL/CREATE_NEW(= `claimExclusiveUtf8`)与 `fsyncDirectoryQuiet`(= `syncDirectoryBestEffortUtf8`)。两文件合同本自同源(atomic_fs.cpp 头注引用 portable 模式);收敛是 2 行级重构,但两文件均在 PR #1338 独占清单内 → 本轨道只记录,不写。已随审计表 row 31/32 归档。

## 5. 复跑命令

```
rg -l "sicnu::portable::(pid|pathFromUtf8|pathToUtf8|envUtf8|wideFromUtf8|utf8FromWide|isWindowsReservedName|claimExclusiveUtf8|fileOpenUtf8|syncFileUtf8|syncDirectoryBestEffortUtf8)" src --glob '!src/platform/portable.h'
rg -n "CreateFileW" src/core
rg -c "GetLastError" src/core
rg -n "GetCurrentProcessId|_getpid|GetEnvironmentVariable|_wfopen|MultiByteToWideChar|WideCharToMultiByte|fsync|FlushFileBuffers|MoveFile|ReplaceFile|fs::rename" src/core -g '!*qgs*'
```
