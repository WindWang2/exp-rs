# R19-03 BASELINE（基线事实，全部 LIVE 实测 2026-10-20）

## 仓库/分支

- `origin/master = e12a9afbc8883a474dc9ffcaf3345ae55863dc71`（fetch 后 `git rev-parse` 实测；主 worktree 本地 master 落后 294 提交、0 ahead，未触碰）
- 本轨 worktree `exp-rs-03-qt-cartography-ux-20261020T102458Z`，分支 `agent/r19-03-qt-cartography-ux-20261020T102458Z`，BASE_SHA=HEAD=e12a9afbc
- 开放 PR：#1663（R18-A 科学算法）、#1664（R18-E 教学/核验）——文件集与本轨写集零交集（#1664 src/app 0 文件已核）
- 未合并远端分支：仅上述两 PR 的 head；r14/r16/r18-B/C/D 旧分支均已被 master 包含

## 相关已合并修复（防重复搬运）

- **#1661（r18-d，本域前任）**：toggleEditing 拒启 lease 泄漏、19 digitizing 命令 gate、activeEditable 画布事实、TaskCenter 去抖投影、外部编辑监听、MapSpec 数字 text/legend title 类型化拒绝、异构页修复 itemSheet、单页 PNG 落位、atlas filename_expression 硬失败、NaN DPI 否定区间守卫。遗留：W1b lease 路由、C4 produce legend 守卫、Edit 菜单无工具栏孪生命令未 gate（R2 P3-5）→ 后两者为本轨 T2/T4。
- **#1662（全项目 sweep）**：#1535 feedback UAF（实读：feedback 重父到 task）、#1536/#1537（实读：STAC generation 令牌在位）、#1561（实读：/vsicurl/ 等走 QtConcurrent+世代防护）、#1562（实读：refilter 已接）、#1563（实读：csvField 转义在位）、#1564（实读：startPending 内联节流+重臂）、#1559（实读：destroyed 释放 seam+身份检查）——以上**不再重复修**。

## 构建/环境

- 40 核 / 62 GB RAM（15 用）/ 352 GB 盘；无活动构建进程
- Configure（EXIT=0）：Debug + ENABLE_TESTS=ON；vendor 前缀 `paleo-singlefactor-closure/vendor/prefix`（GDAL 3.12.2/PROJ 9.7.1/GEOS 3.14.1/GSL/QCA），系统 Qt6 6.12；OTB/Python/3D/Quick/Server/PostgreSQL/Spatialite OFF；GSL、netcdf 显式路径（对齐 r18-d 配方，configure1/2 失败记录在 /tmp/r1903-configure{1,2}.log）；catch2 复用 `~/.cache/exp-rs-fdeps`；链接 stub 本轨独立目录 `/tmp/r19-03-deb`（不与他人共用）
- 资源纪律：`resource_guard.py --lock-dir $GIT_COMMON_DIR/r19-global-j2-build`，`CMAKE_BUILD_PARALLEL_LEVEL=2`，测试 `-j1`，`QT_QPA_PLATFORM=offscreen`

## 关键行为不变量（改动不得破坏）

- `main_window_vector.cpp` save 时 layer id 重解析（commit 后 relocation 防悬空）
- `cartography/export.cpp` 字体替换与多页导出文件名纪律
- r18-d 的 SingleNamePageExporter 单页 sha 确定性、atlas 表达式编译期硬失败、DPI 否定区间守卫消息
- `spectral_profile_widget` 网络源必须留在工作线程（#1561 修复形态）
- `async_algorithm_runner` feedback 生命周期（父到 task）
- QGIS edit buffer / QgsProject / map canvas 原生语义零自造引擎

## 测试基线

- 见 TEST_RESULTS.md（编译完成后填：目标清单、首跑全量结果、任何 pre-existing 失败的排除记录）
