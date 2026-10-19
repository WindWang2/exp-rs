# R18-D Baseline (LIVE recon 2026-10-19)

- 本 worktree: `/home/kevin/project/exp-rs-r18-d-desktop`，分支 `agent/r18-d-workbench-cartography`
- 基线: `origin/master = a60c8831c9cc4e64701d17bde2ace67a74a9b7a0`（"Merge pull request #1513 ... r15-B-geodata-persistence"，2026-10-19T02:17:02+08:00）
- 任务书快照（master=7825c1ebd、#1513 draft）已严重滞后：**#1513 已于 2026-10-18T18:17 合并**，本轨以其结果为基线。
- `gh auth status`: 已登录 WindWang2（repo scope 可用）。
- 开放 PR：仅 #1534（hardening/r10-algorithm-processing-quality，src/analysis + src/agent/harness + data/processing/algorithm_meta）——与本轨写集 **零交集**（overlap_scan: 0）。
- 开放 Issues：#1467（core-fork cluster，src/core/processing）、#1491（CI macOS）——均非本轨域，未被本轨接管，也不阻塞本轨。
- 本轨前身已合并 PR：#1524 (r16-d workbench/cartography)、#1517 (r15-E)、#1512 (r14-D)、#1510 (r13-l)、#1503 (r13-g)、#1482 (r11-d)。#1524 的写集（src/agent/cartography|mapspec、src/app/display、main_window_*、workbench/georef_dual_window、shell/ribbon_controller + 7 个测试文件）即本轨起点。
- stale_branches：r10 分支 ahead 7（即 #1534），其余无冲突。
- narrow_targets：`src/agent/mapspec`、`src/app/workbench` 当前无未合并改动（unwired 基线）。

## 环境与构建

- 40 核 / 62GB RAM（~50GB 可用）；但仓库纪律 `-j2`（`SICNU_BUILD_JOBS_CAP=2`），全局五轨锁 `/tmp/exp-rs-five-tracks-build.lock`（执行时 R18-B/R18-C 正持锁构建）。
- 依赖配方（复制自 r18-c 成功构建的 CMakeCache）：`CMAKE_PREFIX_PATH=/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr/lib/x86_64-linux-gnu`，`GDAL_INCLUDE_DIR=.../usr/include/gdal`，`GDAL_LIBRARY=.../libgdal.so`，`Qt6_DIR=/usr/lib/cmake/Qt6`，GDAL vendor OFF，OTB OFF，Python OFF，ONNX/TensorRT/OpenVINO OFF，`QT_QPA_PLATFORM=offscreen`。
- 构建目录 `build-r18-d`（worktree 内独立）。

## 测试基线

master 上本轨已注册测试（tests/）：test_interactive_session_contract、test_maptool_teardown_r4、test_cartography_quality、test_cartography_visual、test_cartography_production_11、test_cartography_templates、test_cartography_tokens、test_cartography_library、test_cartography_operators_10、test_mapspec、test_georef_dual_window(_workbench)、test_remote_map_display、test_qgis_display_manager、test_workbench_{host,state_model,full_shell_lifecycle,shutdown_policy,enum_provider}、test_r14_D_vector_edit_honesty、test_platform7/9、test_edit_*。
