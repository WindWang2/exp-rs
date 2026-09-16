# EVIDENCE — 本地可复现证据日志（Local evidence only; no online CI dependency）

格式：日期 | 命令 | exit | 关键输出 | 结论。所有 claim 必须可映射到本文件某行。

## Phase 0

- 2026-09-15 | `git fetch origin --prune && git rev-parse origin/master` | 0 | `a5b11b7f10fa010c1c060864fb427d777ba9a4aa` | 基线 SHA
- 2026-09-15 | `gh pr list --state open` | 0 | 1 open：#1008 spectral（CONFLICTING） | 并发面
- 2026-09-15 | `gh pr diff 1008 --name-only` | 0 | 39 files，SAR 主写域零交集 | PARALLEL_OWNERSHIP
- 2026-09-15 | `gh issue list --state open` | 0 | #1001–#1007，无 SAR 条目 | OUT_OF_SCOPE dedupe
- 2026-09-15 | `git worktree add ../exp-rs-advanced-insar-platform-11 -b zcode/advanced-insar-platform-11 origin/master` | 0 | HEAD a5b11b7f | worktree 建立
- 宿主资源观测方式：Windows/Git Bash；RSS 经 `tasklist`（或 PowerShell Get-Process）；
  负载平均在 Git Bash 下不可测（uptime 无意义）→ 按约定记录一次 not-executed，
  构建恒定 `-j2` 上限。

## OUT_OF_SCOPE

- #1001/#1002/#1003/#1004/#1005/#1006/#1007（io/workflow/dataset/georef/agent 领域缺陷，
  非本 track 主写域；未修复，留原 track）。
- ISSUES.md S-1/S-2（多时相变化日历语义 / 极化分解）——非 InSAR mission，不在本 track。

## not-executed

-（暂无；随阶段回填）

## 测试证据

-（随 Phase 回填：命令、exit、测试计数）

## 测试证据（2026-09-16，全部本地可复现）

- build-dev configure | exit 0 | dev-default preset + Ninja + MSVC 14.38 + vcpkg toolchain
  （附加发现： FETCHCONTENT_SOURCE_DIR_CATCH2=C:/deps/catch2-src、
  CMAKE_PREFIX_PATH 需含 qca-install/kc-install、BISON/FLEX=winflexbison——全部入
  _insar11_configure.cmd，见下）。
- 首次全量构建 1506 targets | exit 0 | 零编译错误。
- InSAR 11 targeted 套件 34/34 组 PASS（_insar11_runtests.sh，ctest -j1，offscreen）：
  baseline×4 / topo×6 / coreg×4 / closure×3 / provider×4 / pair×4 / inversion×4 /
  platform11×5。
- 回归：test_sar_orbit（7 case）、test_sar_insar（unwrap/displacement 组）、
  test_sar_operators（calibrate/backscatter/speckle/ratio 组）全部 exit 0。
- drift gates：
  - test_algorithm_meta_drift | PASS | pin 32→43（a5b11b7f 基线 39 + 本 track 4）；
    再生 `sicnu_geo_rs_cli --export-catalog`（43 sidecar）；删除 4 个上游已不再生成的
    rs-temporal-* sidecar。
  - contract_inventory 再生 | exit 0（dedupe 后 findings=0）；test_contract_platform_9
    全部 PASS（含 snapshot freshness 字节比对）。
  - test_capability_drift | PASS（补 7 个上游缺失条目：mnf_inverse/spectral_band_select/
    library_select + io:catalog_search/cube_plan/cube_window/cache_prefetch）。
- 宿主环境记录：Git Bash 下 ctest/exe 需要 PATH 含 Qt bin/qca/kc 且 PROJ_DATA/
  GDAL_DATA 指向 vcpkg share（包装脚本已固化）；负载平均不可测 → 恒 -j2（已按约定
  记 not-executed）。

## OUT_OF_SCOPE（本 track 顺带修复的 master 阻断，PR_BODY 顶部披露）

- src/workflow/pipeline_run_coordinator.cpp：Q_OS_WIN 分支缺 <fcntl.h>
  （c5d4aafe 引入，_O_WRONLY/_O_BINARY 未定义，MSVC 全体构建破坏）→ 1 行 include。
- src/agent/data_platform_tools.cpp：裸 BenchmarkService/benchmarkRunStatusToString
  未限定 sicnu::experiment（a5b11b7f 引入，MSVC sicnu_agent 构建破坏）→ 4 处限定。
- data/agent/capabilities/io.json + preprocess.json：gaofen/hj/zy3_import 能力条目
  三重复制 → 去重（保留 preprocess 富集版）。
- data/processing/algorithm_meta：4 个 rs-temporal-* 上游陈旧 sidecar → 删除。
