# BASELINE — r9-science-data-integrity

- 采集时间：2026-10-12（实时查询，非快照）
- `git fetch origin --prune`：exit 0
- 远端分支：仅 `master` @ `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`
- `gh repo view`：defaultBranchRef=master
- 开放 PR：0
- 最近合并 PR：#1474(2026-10-11T18:35:02Z)、#1473(18:33:57Z)、#1472(18:33:48Z)、#1471(18:30:11Z)、#1470(18:29:06Z)、#1469(18:24:46Z)
- 开放 Issue：20 个（#1447–#1468 非连续）；A 向核心相关：#1461 #1456 #1457 #1458 #1467 #1450 #1448 #1449 #1447 #1464（#1465 归 C 协调、#1466 部分 suitability、#1468 测试 infra 归 C）
- worktree：`exp-rs-r9-science-data` 分支 `hardening/r9-science-data-integrity`，BASE_SHA=8a08642760e2e8ffd2c4b965fc4683c01d3e4758，merge-base==HEAD（干净）

## 构建环境（Windows/MSVC lane）

- MSVC 2022 (14.38.33130) x64，Ninja，Qt 6.8.0 (C:\deps\Qt\6.8.0\msvc2022_64)，vcpkg toolchain（GDAL 3.12.4 / PROJ 9.8.1 / GEOS 3.14.1 / OpenCV 4.12.0 / Protobuf 33.4.0 / curl 8.21.0 等，binary cache 恢复），Catch2 本地源（C:/deps/catch2-src）
- 配置：`configure_r9.cmd`（等价 dev-default + 工具链接线）；构建：`build_r9.cmd`，`CMAKE_BUILD_PARALLEL_LEVEL=2`；CTest `-j1` offscreen
- 内存基线：32GB 物理，采集时 ~4GB 空闲（>70% 压力）→ 构建 -j2 上限、大测试串行；磁盘 543GB 空闲
- Windows 特记：`resources/icons` 在 git 为 symlink blob → 检出为 33B 文本占位；以目录 junction 指向 tracked 权威源 `docs/design/ui/svg-icons/icons`（198 文件）。主 worktree 同样以真实目录存在（其 git status 即 `D resources/icons`）。junction 不入提交。

## 首次构建记录

1. configure：exit 0（180.8s configure + 63.5s generate）——依赖摘要见 configure_r9.log
2. build 第 1 次：ninja 找不到 build.ninja（与 configure 并发竞争，构建脚本本身在 configure 完成前启动）→ 重启
3. build 第 2 次：`resources/icons/Fill_NoD_t_.svg` missing（上述 symlink/Windows 问题）→ junction 修复后第 3 次启动
4. build 第 3 次：见 GOAL_LOOP_LEDGER 轮次记录（进行中）
