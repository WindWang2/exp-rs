# R21-C BASELINE — Pi/MCP Harness、科学决策、Workflow 与插件工具契约

## 执行时刻基线（LIVE，2026-10-23T18:43Z–19:00Z UTC）
- `origin/master` = `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（Merge PR #1672, r20-04）
  - fetch 复核方式：git fetch 直连 github.com:443 失败（网络分区：api.github.com 可达 / github.com 不可达，curl 实证），改用 `gh api repos/WindWang2/exp-rs/commits/master` + `compare/911e0b17...master` 双确认：`identical`（ahead=0/behind=0）。本地 origin/master 引用与远程一致。
- Open PR：0（`gh pr list --state open` 实时）。最近合并：#1668/#1669/#1670/#1671/#1672（R20 五轨全并）。
- Open Issues：126（`gh issue list --state open --limit 500`，存 `/tmp/r21c-issues-open.json`）。
- 并行轨：R21-A（worktree `exp-rs-r21-A-20261023T184835Z`，vendor 前缀配方 configure）、R21-B（`exp-rs-r21-B-20261023T185101Z`，正持锁构建）。共享构建锁 `/home/kevin/project/exp-rs-build.lock`。
- 旧 `agent/r20-*` 分支已并入 master（behind-only，不建 worktree——与任务书一致）。

## Worktree
- `agent/r21-c-pi-mcp-harness-20261023T185308Z` @ `/home/kevin/project/exp-rs-r21-C-20261023T185308Z`，BASE=911e0b17a3。
- 首次 `git worktree add` 因部分克隆 lazy-fetch 走 github.com 失败一次（TLS eof）；实证 origin/master 树 15385 blob 全在本地（`git ls-tree -r` + `cat-file --batch-check`，missing=0）后重试成功。
- `.r21-local-baseline.txt` 为运行现场资料，不入 PR。

## 构建配方（vendor 前缀 + 系统 expat/zlib/zstd 覆盖 + link-shim，最终版）
- 纯系统依赖配方失败（GDAL_DIR 缺失，`/usr` 无 gdal 头）；复用 R21-A 同款 vendor 前缀配方并追加两组修复（R21-C 现场 debugging）：
  1. **静态非 PIC 库**：vendor 前缀的静态 `libexpat.a/libz.a/libzstd.a` 链入共享库 libqgis_core 直接 `final link failed: bad value`；且 CMake 的 `*_LIBRARY_RELEASE` 优先于 `EXPAT_LIBRARY`，须覆盖 `EXPAT_LIBRARY_RELEASE`/`ZLIB_LIBRARY_RELEASE`/`ZSTD_LIBRARY`。
  2. **wrapper2 LAPACK 符号**：vendor libgdal.so 未定义 `wrapper2_dposv_` 等 26 个符号（其 DT_NEEDED libarmadillo.so.15 在 vendor 前缀不存在），全主机无提供者。采用本主机既有模式（R19-02/R21-B 同款）：`build/link-shim/`——`libopenblas.so.0`（26 个 wrapper2_* 转发桩）+ `libarmadillo.so.15`/`libarpack.so.2` 小桩 + `libblas.so.3`/`liblapack.so.3`/`libpcre2-*` → /usr/lib 符号链接；`-DCMAKE_EXE_LINKER_FLAGS="-L<build>/link-shim -Wl,--allow-shlib-undefined"`。
- **运行**：DT_RUNPATH 不传递，须 `LD_LIBRARY_PATH=<build>/link-shim:<vendor-prefix>`（否则 libgeos.so.3.14.1/libarmadillo.so.15 解析失败）。vendor 前缀的 Qt 旧于系统 Qt6.12；链接已用系统 Qt，canary 实测可加载运行。
- configure exit 0（/tmp/r21c-configure5.log）；canary（test_capability_knowledge）链接+运行全绿（1637 断言/17 例，与 R20-03 记录一致）。
- 并行：`CMAKE_BUILD_PARALLEL_LEVEL=2`，经 `/home/kevin/project/exp-rs-build.lock` flock 串行（与 R21-A/B 互斥）；测试 `-j1`、`QT_QPA_PLATFORM=offscreen`。
- 49 个 C 轨测试/工具目标（/tmp/r21c-build-targets.txt）。

## 核心链路（继承 R20-03 BASELINE，live 复核待 M1 构建后跑回归确认）
`pi/exp-rs-spatial.ts` → `pi/mcp_bridge.ts`（代际化子进程、4MiB 行界、EOF 半帧、>32MiB runaway kill）→ 宿主 `--mcp`（src/agent/mcp_server.cpp）→ tool dispatch（tool_catalog/grounding_tools/data_platform_tools/…）→ workflow/TaskCenter（src/workflow、src/agent_ops）→ scientific evidence（src/science_context、src/preflight）。
- R20 #1670 已修（绝不重做）：IPC Response `result` 类型门（E6002）、capability 三加载器 fail-closed、decision_record 类型门、TooLarge hexdump 抢救、OpsRunRequest 死字段、rs:sam_classify sidecar drift。
- R19-02 #1666 已修（反回滚守卫范围）：#1551 services 门（commit 77d70df94d）、#1549 allowAssumed 缓存投影（65703e48f8）、#1595 cloud-max/limit ok 门、#1597 dataset 侧 rejectUnknownArgs、#1588 temporal 多集合计数、#1592 peerString 门、#1539/#1540 审批令牌、#1552 journal 深度门、#1594 placeholder 单遍。

## 种子 issue 现场分诊（详见 ISSUE_TRIAGE.md）
- FIXED-OPEN（写复测证据后建议关闭）：#1551、#1549、#1588、#1595（主体）、#1539、#1540、#1552、#1589、#1592、#1594、#1654。
- 半修（本轨工作包 W-A/W-B）：#1597 剩余 pipeline/workflow/catalog-export/data 四处 leftover 黑洞；#1595 同族残留 `--max-items` 无 ok 门。
- 候选工作包：R20-03 W8 记录的 plugin 诊断码族 5 二进制 master 同构红（E5005/symlink TOCTOU/LibraryLoadFailed/TrustRejected/ManifestInvalidJson）；D5 mProcessHandle 无锁读；D6 无界文件读（workflow_run_lock/capability_mirror/sdk 读取）；D9 CLI `--approve` 静默 no-op（CT-B）。
- 维持 DEBT（R19-02/R20-03 决策，除非本轮找到行为可观察差异）：#1628-3 temporal phenology 接线（功能面，M4 候选）、#1629-1 run/resume 重复、#1630-2 EnumTable。
- 跨轨不修：#1598/#1599（E 轨 verification）、CT-6 cartography schema（04/D 轨）。

## 风险
- 网络分区：github.com 不可达 → push/PR 提交可能受阻（gh api 可达，`gh pr create` 走 api，待验证；git push 走 github.com 会失败）。已备选：PR body 全部写好、commit 全部本地，若 push 失败如实记录阻塞。
- 五轨共机：构建串行慢（R21-B 持锁中），测试全程 -j1。
