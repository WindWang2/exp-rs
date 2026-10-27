# R24-A Science Track — Baseline

- UTC: 2026-10-27T02:27Z（侦察开始）/ 文档更新随循环推进
- Base: `origin/master@5fe642ec6339807823aa56612ba94668592a738d`（PR #1717 合并后 HEAD）
- 本地主仓 master 落后于 `ad98a7a70`（只读，不动）
- Worktree: `C:\Users\wangj.KEVIN\projects\exp-rs-r24_a-20261027T022732Z`
- Branch: `agent/r24_a-20261027T022732Z`（自 origin/master 新建，带唯一时间戳）
- git status（worktree 起点）：干净；主仓有未跟踪 `data/samples/manifest.json`（他人产物，只读保留）

## 现场快照（2026-10-27T02:2xZ 实测）

- 开放 PR：**0**（`gh pr list --state open`）
- 开放 Issue：**144**（大量为"已修未关"，见 ISSUE_TRIAGE.md）
- 远端分支：`master` + 两条 r23 残留（`agent/r23-d-lab-grading-evidence-20261026T165151Z`、`agent/r23-e-platform-plugin-release-20261026T170017Z`）；**无 B/C 并行分支在跑**
- 本地兄弟 worktree：`exp-rs-D-20261026T165151Z`（r23-D，绕开不动）
- 最近合并：#1717 (r23-D)、#1716 (r23-E)、#1715 (conflict-marker fix)、#1714 (r22-agent)、#1713 (r22-workbench)、#1712 (r22-science 数值正确性)、#1711 (r22-teaching)、#1710 (r22-data)、#1709 (r22-inference)、#1708 (r22-infra)、#1677/#1676/#1675/#1674/#1673 (r21 系列)

## 环境实测

- Windows 10.0.26200 x64，Git Bash；RAM 32GB（侦察时空闲 ~10.6GB）→ 构建 `-j2`（上限 4，仅在 ≥12GB 空闲且无其他构建者时）
- CPU 16 逻辑核；磁盘空闲 ~711GB
- 工具链：VS2022 MSVC 14.38（vcvars64）+ Ninja（C:\Qt\Tools\Ninja）+ CMake toolchain `C:\deps\vcpkg\scripts\buildsystems\vcpkg.cmake`
- 依赖：Qt 6.8.0 (C:\deps\Qt)、GDAL 3.12.4 / PROJ 9.8.1 / GEOS 3.14.1 / OpenCV 4.12.0（vcpkg_installed，3.0GB，由主仓 build-dev 同 manifest（md5 502a26e6…）复制到本轨 `build-r24_a/vcpkg_installed`）
- Build dir：`build-r24_a`（本轨独立）；`CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`
- 已知环境性失败（CT-R21-A-1，继承自 r21-A）：`test_d16_temporal_phenology_e2e` 在含 8 位数字的路径下因 `src/core/temporal_cube.cpp` sceneInstant 的 `\d{8}` 扫描误判采集日期而失败——本 worktree 路径含 `20261027`，该测试如失败按环境性记录，不算本轨回归（src/core 归 02/B 轨）

## 关键 PR 精读结论（防重做）

- **#1712 (r22-science)**：#1707 F-H 精确 EDT 下包络覆盖、#1684 computeStatsFromAccumulators float 均值截断、#1685 portable_random.h 替换 9 处 std::shuffle/uniform、#1686 resampler 半像元足迹。全部在 master。
- **#1676 (r21-A)**：17 项科学缺陷（dos2 airmass 门、kmeans scale flag、校准类 id 门、SAR DEM 单位、continuum 零包络、OLS 零和、OBIA 重映射 0 哨兵、WindowMean、monitor 零方差、DOY 366、Rayleigh 相函数、DN→TOA Inf 门、radiometric bands 非整数、RF getVotes、terrain_flatten NaN 方位、continuum 外推 clamp、RsSegmentMap 竞态）+ gamma0 三处公式统一 + dNBR 单遍融合。
- **#1671 (r20-01)**：趋势时间戳配对、viewshed NoData、镶嵌 X 翻转、分类器标签契约、SAR 角域、InSAR 大场景条件数。
- **#1663 (r18-a) / #1534 (r10-A)**：Inf 治理、确定性 KMeans、typed 参数契约、staged raster publication。

## 实读路径

src/processing/algorithms/**（全目录清单见 ALGORITHM_INVENTORY.md）、src/analysis/{classification,segmentation}/**、src/operators/rs/**（科学算子接线）、tests/CMakeLists.txt（末尾 append 区）、nodata_utils.h、math_utils.cpp、CLAUDE.md、.agents/AGENTS.md、CMakePresets.json。
