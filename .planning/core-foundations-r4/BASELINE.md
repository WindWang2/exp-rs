# Phase 0 Baseline — core-foundations-r4 (Track 5)

Recorded: 2026-09-27, worktree `/home/kevin/project/exp-rs-core-foundations-r4`, branch `hardening/r4-core-foundations`.

## 1. 实测基线

- `origin/master` = `15e5c66b5` ("Merge pull request #1333"),`master..origin/master` = 0/0(fetch 后实测,与提示词写作值一致,未漂移)。
- 主仓根目录未做任何写操作;全部工作在本 worktree + 全新构建目录 `build-r4/`(Debug, Ninja, ENABLE_TESTS=ON, PCH ON)。
- 工具链实测:`cmake 3.30.5`(`/home/kevin/toolchain/cmake-dist`)、`ninja`(`~/.pwb-sdks` 前缀 `/home/kevin/pwb-sdks/root/usr/bin/ninja`)、编译器 `/usr/sbin/c++`、Qt 6.11.2、GDAL 3.13.3(`/home/kevin/pwb-sdks/root/usr`)、`gh` 已认证(WindWang2)。无 ccache。
- 配置踩坑记录(供复跑):直接 configure 会因 ninja/GSL 探测失败,须带 `-DCMAKE_MAKE_PROGRAM=…/pwb-sdks/…/ninja -DCMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr -DGSL_ROOT_DIR=/home/kevin/pwb-sdks/root/usr`。

## 2. 在途 PR 盘点与 file-overlap map

实测 open PR(2026-09-27,gh):

| PR | 分支 | 文件数 | 与本轨道白名单的重叠 |
|---|---|---|---|
| #1334 fix/review-p1-security | fix/review-p1-security | 29 | **`src/core/plugin_host.{h,cpp}`(#1334 独占,避让)、`tests/CMakeLists.txt`(共享,末尾追加式改动,rebase 可解)** |
| #1335 fix/review-p0-build-restore | fix/review-p0-build-restore | 19 | `tests/CMakeLists.txt`、cmake/*.cmake(共享;#1335 另动 `src/agent/CMakeLists.txt` 不在本轨白名单) |
| #1336 hardening/closure-ui-runtime-r4 | closure-ui-runtime-r4 | 13 | 无与白名单重叠(tests UI 面 + data/help + scripts) |
| #1337 hardening/closure-workflow-contracts-r4 | closure-workflow-contracts-r4 | 22 | 无(`src/contracts/error_code_scanner.*`、`graph_assembly.cpp` 非 `scientific_contract.cpp`;workflow 面不重叠) |
| #1338 fix(io,processing) closure-io-processing-r4 | closure-io-processing-r4 | 28 | **最大重叠:#1338 独占 `src/geospatial/util/atomic_fs.{h,cpp}`(合同定义面)+ 调用方 `raster_writer.cpp`、`vector_writer.cpp`、`raster_convert.cpp`、`staged_raster_output.h`;全部避让,审计表标注 defer-to-#1338** |

**避让清单(本轨道不写)**:`src/geospatial/util/atomic_fs.{h,cpp}`、`src/geospatial/raster/raster_writer.cpp`、`src/geospatial/vector/vector_writer.cpp`、`src/geospatial/convert/raster_convert.cpp`、`src/processing/gdal/staged_raster_output.h`(#1338 独占);`src/core/plugin_host.{h,cpp}`(#1334 独占)。
**共享文件**:`tests/CMakeLists.txt`(#1334/#1335 亦改)——本轨道仅追加新测试目标注册行,若上游先合并则 rebase 解决(纯追加冲突,可机械合并)。
`git fetch origin hardening/closure-io-processing-r4` 的 diff 已读(只读参考):#1338 对 `atomic_fs.h` 的改动为新增重载/注释面,与 #1323 建立的 publish 语义同向;本轨道合同测试按**合同语义**(而非当前实现细节)书写,以便合并后仍有效。

## 3. 评审材料通读

- 实测缺失:`PROJECT_REVIEW_DOSSIER_5.0.md`、`AUDIT_DOSSIER_ISSUES_747_760.md`、`PR_TRIAGE_REPORT_2026-09-16.md`、`docs/PARALLEL_TRACKS_10.md` 均不在工作树(亦无 git 历史)→ 按预案以 open PR 完整描述 + `WHOLE_REPO_REVIEW.md`、`ISSUES.md`、`TEST_INFRA.md` 为评审输入。
- #1335 描述中的既有失败测试存量与 #1336 未解决项:以本地基线红绿分布(§6)实测为准,不沿用转述。

## 4. 开放 issue

`gh issue list --state open` 实测 = **0 个**。

## 5. 第三节锚定表复核(全部以本 worktree `15e5c66b5` 实测)

| 锚点 | 提示词值 | 实测值 | 命令 |
|---|---|---|---|
| `src/core` 源文件数 | 2047 | **2047** | `find src/core -type f \( -name '*.cpp' -o -name '*.h' \) \| wc -l` |
| `CreateFileW` 直调(src/core) | 1 处 | **1 处**:`src/core/qgsfileutils.cpp:340` | `rg -n "CreateFileW" src/core` |
| `GetLastError` 直调(src/core) | "12 处" | **93 处出现 / 12 个文件**(提示词的 12 是文件数);全部位于 QGIS vendor 文件 `qgs*.cpp`(ogr/gdal providers、browser、vectorfilewriter、offlineediting、arrowiterator) | `rg -c "GetLastError" src/core` |
| `_wstat`/`WideCharToMultiByte`/`LoadLibraryW` | 0 | **0** | `rg -c` 无匹配 |
| `atomic_fs` 匹配文件 | 35 | **35** = 30 业务调用方(.cpp/.h)+ 合同面 2 + CMakeLists 链接面 2 + `portable.h` 引注 1(名单固化于 ATOMIC_FS_SEMANTIC_MATRIX.md) | `rg -l "atomic_fs" src` |
| `portable.h` helpers | 6 个 | **11 个**(master 已前进):`pid`/`wideFromUtf8`/`utf8FromWide`/`pathFromUtf8`/`pathToUtf8`/`envUtf8` + `isWindowsReservedName`/`claimExclusiveUtf8`/`fileOpenUtf8`/`syncFileUtf8`/`syncDirectoryBestEffortUtf8` | 通读 portable.h |
| UTF-8 约定头注 | portable.h:14 | portable.h:14 ✓("std::string path values hold UTF-8 bytes") | 通读 |
| atomic_fs 测试链接历史 | #1303/#1304 两次补链 | git log 证实;现役载体 `tests/test_io_atomic_failures.cpp`(轻量 `sicnu_add_io_test`,只链 Catch2+Sicnu::Geospatial) | `git log --oneline --all -- src/geospatial/util/atomic_fs*` |
| 合同面 API(h 行号) | fileExists h:36 / renameReplaceQuiet h:59 / removeFileQuiet h:63 / writeFileAtomic h:85 | ✓ 全部吻合;另有 `stagedPathFor`/`fsyncFile`/`publishStagedFile`/`publishStagedGroup`/`publishStagedMembers`/`discardStaged`/`sidecarsFor`/`fileSize` | 通读 atomic_fs.h |

### 失败注入对象枚举(≥20 类候选,入选判据:对外导出头 + 被 ≥2 上层模块调用,或合同面单点但语义承载 publish/durability)

合同面(atomic_fs,9):writeFileAtomic、publishStagedFile、publishStagedGroup、publishStagedMembers、stagedPathFor、fsyncFile、renameReplaceQuiet、removeFileQuiet、discardStaged。
portable 面(4):claimExclusiveUtf8、fileOpenUtf8、syncFileUtf8、envUtf8。
调用方承载 publish/durability 语义的高频类(≥10,按调用方普查选定):session_journal、scientific_contract、stage_ledger、fabric/mirror、finalize_manifest、workflow_checkpoint、artifact_gc、workflow_run_coordinator、output_committer、cli_commands(+ io_fabric_operators、detection_tile_engine 视进度)。
**终版 20+ 类清单随 WP-C 首提交固化进测试文件头部注释与 BASELINE 附录。**

## 6. 基线红绿分布(实测,2026-09-27 回填)

轻量闭包(与本轨道改动无闭包交集的 `test_io_atomic_failures` 只链 Catch2+Sicnu::Geospatial,且合同面文件本轨 defer 未动):

| 目标 | 结果 | 备注 |
|---|---|---|
| test_io_atomic_failures | **8/12 绿,4 红(预存)** | 4 红全部 = `VectorWriter::create: dataset creation failed`(ESRI Shapefile);根因 = `stagedPathFor` O_EXCL 预创建 0 字节 staged 文件,GDAL Shapefile Create 拒绝已存在目标(GPKG 因 SQLite 视 0 字节为合法空库而通过)。**该缺陷即 PR #1338 diff 中自述的 Cluster A ~28**,修复(`reservedStagedPathFor`)在 #1338 分支,根因文件全在本轨避让清单 → 记预存红、defer-#1338,与本轨道改动无因果(CPL_DEBUG/源码级定位证据见 EVIDENCE.md §1) |
| test_platform_portability / test_portability_source_contract / test_portability_contract | 全绿 | — |
| 其余正则命中重型 7 目标 | 构建完成后双跑回填 EVIDENCE.md | — |

本轨道新增测试(合同/注入/压力/UTF-8/portability pin)首跑即全绿,无基线红。

## 7. 本轨道边界声明

白名单(允许写):`src/core/`(避让 #1334 的 plugin_host.*)、`src/platform/portable.h`、35 个 atomic_fs 匹配文件中**不属避让清单**者、`tests/`(新测试文件 + tests/CMakeLists.txt 追加行)、`.planning/core-foundations-r4/`。
明确不做:新增功能/算子/工作台/实验;避让清单文件;等待线上 CI;触碰其它轨道 worktree。
账本:`/.goal-loop-ledger.md`(worktree 根,已加入 `.git/info/exclude`,保持未跟踪;#1336/#1337 将各自账本随 PR 提交属其轨道选择,本轨道按提示词保持 gitignored)。
`.planning/core-foundations-r4/` 默认被 `.gitignore`(`.planning/*`)忽略 → 证据文件以 `git add -f` 显式入库(与先例轨道一致,评审可复核),不修改 `.gitignore` 本身(避免与在途 PR 冲突)。
