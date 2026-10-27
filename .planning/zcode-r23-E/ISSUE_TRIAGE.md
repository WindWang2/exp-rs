# ISSUE_TRIAGE — R23-E（E 轨相关开放 Issue 分类）

分类法：LIVE（当前 master 可复现/证据成立）/ FIXED_OPEN（已修但 issue 未关）/ DUPLICATE / STALE / IN_PROGRESS / OUT_OF_SCOPE（非本轨）/ UNVERIFIABLE。
验证方法：`git merge-base --is-ancestor <fix> HEAD` + 逐 hunk 源码核对（证据行号）；FIXED_OPEN 记录修复 commit 与回归测试缺口。

## FIXED_OPEN（修复在 master，含证据）

| Issue | 修复 commit | 证据 | 回归测试 |
|---|---|---|---|
| #1551 plugin.load services 缺 isObject 门致 std::terminate | 77d70df9 (PR #1662) | `src/plugins/host/plugin_host_worker_main.cpp:1086-1121`（services 对象门 + 四成员 peerString 门，E1002 typed） | **无** → WP1 |
| #1592 执行路径 params.get().asString() 无类型门 | 77d70df9 | `plugin_host_worker_main.cpp:471-478 peerString()`；operatorId:635、workDir:670、toolId:1389、providerId:746、uri:768-776（类型检查先于 856/858 的 get().asString()，908 在 try/catch 内） | **无** → WP1 |
| #1644 plugins 技术债（workerPid 快照等） | 77d70df9 + 34feec4b | killProcess 走 mLastKnownGroup 写_once 快照（session.cpp 699 区）；workerPid 快照测试已有（test_plugin_host_process.cpp:392） | 部分（pid 有，kill race 无直接测试——复用既有 crash/restart 族） |
| #1555 worker 池持 nodesMutex 触发 pending 回调 | 58b935f7 | python_worker_process_pool.cpp 重构 | 无 → WP2 |
| #1556 迁移失败路径泄漏 __shm_key__ | 58b935f7 | app_interface_bridge.cpp:39-51（migrateRasterInputToShm 失败路径 remove）+ daemon 侧 549-577 | 无 → WP2 |
| #1608 execute_algorithm 原始返回值直给 json.dumps | 58b935f7 | worker_daemon.py:23-28（ndarray/np 标量 typed 转换层） | 无 → WP2 |
| #1609 unload_plugin 只清理部分注册表 | 58b935f7 | worker_daemon.py unload 路径 | 无 → WP2 |
| #1647 python 技术债（2 项） | 58b935f7 | 同上 | 无 → WP2 |
| #1557 macOS 通知图标 autorelease UAF | 1b3d28f1 | qgsmacnative.mm:44-77（显式 release/持有注释 #1557/#1641） | 无（macOS 无法本地执行 NOT_EXECUTED；静态核对） |
| #1617 DBus NoBlock 后检查 lastError() 无意义 | 1b3d28f1 | qgslinuxnative.cpp:55-67/77-90（QDBusPendingCallWatcher 异步回退） | **无** → WP5 评估（headless DBus 不可得时记录 UNVERIFIABLE-Linux） |
| #1618 openTerminalAtPath 一律追加 --working-directory | 1b3d28f1 | qgslinuxnative.cpp:130-153（按终端类型分派 --workdir/--working-directory） | 无 → WP5 |
| #1641 native 技术债 | 1b3d28f1 | 同上 | — |
| #1624 vendored GDAL/PROJ/GEOS ExternalProject 并行失控 | 496b9082 | cmake/vendor_deps.cmake:16-21（SICNU_VENDOR_BUILD_JOBS=2 钉死） | check_presets.py JOBS_CAP=2（存在）→ WP7 复验 |
| #1623 sicnu_zh_CN.qm 无 install 规则 | #1708 及此前 | CMakeLists.txt:978-1011（lrelease 产物 install beside exe，缺失即 fail-loud） | **无** → WP4 实机 install 验证 .qm 落树 |
| #1558 两个回归用例断言恒真 | 0847c5ec | test_e2e_open_issues.cpp 重写 | 有（用例本身） |
| #1622 test_spectral_profile_widget 墙钟断言 | 后续修复 | :462-475 注释"measured for the record only"，无断言 | 有 |
| #1697 离线打包 --version 路径穿越删除 | b9ec540e (#1708) | build_offline_bundle.{sh,cmd} 白名单 + realpath 包含性断言 | tests/test_r22_infra_bundle_guard.sh（存在）→ WP4 复跑 |
| #1698 安装树无 RPATH/worker 未安装 | b9ec540e (#1708) | 根 CMakeLists CMAKE_INSTALL_RPATH + sicnu_worker install | #1708 的 tier1/3 smoke（未入库为 ctest？）→ WP4 在当前 master 重做实机证明 |
| #1681 clang-tidy 构建前误阻断 | fe677fcc (#1708) | .github/workflows/ci.yml lint 后置 + merge-base | CI YAML（不等待线上） |
| #1692 IPC >1MiB 响应静默丢弃挂起 | cb3915e2 (#1714) | daemon 发送侧 typed error + 宿主锚定抢救 | test_r22_agent_ipc.cpp（已有） |
| #1691 超时插件加载幽灵注册 | 2af3f1fd (#1714) | 失败路径撤销 + ~AppInterfaceBridge 幂等 | test_r22_agent_ghost_algo.cpp（已有） |

## LIVE（当前 master 证据成立，本轨处置）

| Issue | 证据 | 处置 |
|---|---|---|
| #1699 墙钟阈值断言 flaky（残余 ~9 处） | tests/test_model_failure_matrix.cpp:1000 (<500ms)、test_execution_governor_11.cpp:376 (<500)、test_temporal_profile_widget.cpp:170 (<200)、test_feature_scatter.cpp:111 (<40)、test_progress_dialog.cpp:112 (<100)、test_adversarial_m5.cpp:173/181 (<200 条件)、test_tool_call_dispatcher.cpp:990 (<2000)、test_agent_ops_ui_smoke.cpp:72 (<200) | 文件均属 C/D 轨生产测试（E12 规则：跨轨测试只读）→ CROSS_TRACK_FINDINGS.md 逐条给出转换方案（sicnu_test_time_budget / hang-guard 模式，#1708 已定型） |
| #1682 Windows/macOS 平台零编译型测试（残余：Windows 依赖栈重构） | #1708 只做了 PR-lane 冒烟 + opt-in | 本轨补 Windows `.cmd`/`.ps1` 真机侧脚本级证据（Windows 宿主在场）+ CROSS_TRACK 记录依赖栈重构建议 |
| #1683 CI/脚本卫生（残余：根目录遗留脚本） | repo 根 build_r6.cmd/build_r8sci.cmd/build_r8sci2.cmd/build_wb7.cmd/configure_*.cmd/run_*.cmd 等 | E 域工程脚本：盘点+清理方案（不删数据，最小化）→ WP7 |
| #1491 master 无完成 CI 结果 | gh 运行记录 | 不等待 CI 原则；记录 OBSERVATION |

## OUT_OF_SCOPE（非 E 轨写区，仅记录）

#1704/#1705/#1690/#1693（C 轨 agent 域，#1714 已修）、#1694/#1695/#1688/#1689（UI/教学域；#1688 bundleBuilderRequest cmd 注入——归属按代码位置复核后记录 CROSS_TRACK）、#1678（teaching）、#1551 之外的 src/sdk、#1550/#1546-#1548/#1535-#1544 等算法/数据域、#1604/#1602/#1603（runtime，B 轨）、#1655-#1658（P3 测试债——C/D 域测试文件，复核状态后记 CROSS_TRACK）。

## 复核方式声明

- 所有 FIXED_OPEN 行的 commit 均通过 `git merge-base --is-ancestor <sha> HEAD`（HEAD=ad98a7a70）验证存在且未被后续合并丢弃；关键行号以当前工作树源码为准（rg 直读）。
- 不重新实现 FIXED_OPEN 项；只为其中零测试的关键修复补回归 guard（WP1/WP2/WP4）。
