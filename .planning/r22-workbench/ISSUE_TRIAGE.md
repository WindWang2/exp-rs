# R22-05 ISSUE TRIAGE（动态验真，@911e0b17a3 源码实证）

## 第一批（任务书指定）

| Issue | 分级 | 证据 |
|---|---|---|
| #1695 | **OPEN_UNFIXED（仅第 1 项在本轨）** | src/app/dialogs/async_runner_base.h:52 仍为裸 `QWidget*`；async_gdal_runner.cpp:12-25 / async_algorithm_runner.cpp:20-31 析构不调 `endRun()` → 任务运行中析构后 `setOverrideCursor(WaitCursor)` 永不恢复；async_gdal_runner.cpp:39 从可能悬垂的裸指针构造 QPointer。第 2/3/5 项（teaching_admin_dock i18n 复数/GUI 阻塞、batch_assessment 原子性）在 `src/app/teaching_admin/**`（白名单排除）+ `src/lab`、`src/teaching_admin`（不在白名单）→ OUT_OF_SCOPE（记 CROSS_TRACK） |
| #1557 | **FIXED_OPEN** | master 已由 `1b3d28f1b7`（fix(src/native): resolve full-review findings #1557 #1617 #1618 #1641）修复：QgsUserNotificationCenter 析构显式 `[_qgsUserNotificationCenter release]`/`[_qgisIcon release]`；setIconPath 赋值前 `[... release]`。且 src/native 不在本轨白名单 → 不修，记录 |
| #1655 | **IN_PROGRESS_PR / CONTESTED** | SICNU_CP11_RENDER 门无 CI 归属方属实（.github/workflows/ci.yml 只设 SICNU_MODELS_DIR / -DSICNU_VENDOR_GDAL=OFF，ctest 不带该变量）。但 `tests/test_cartography_production_11.cpp` 在 open PR #1677 的文件集内（+249 行）→ 该文件归 #1677 轨所有，本轨不写；记 CROSS_TRACK_FINDINGS 建议 CI 归属方讨论 |
| #1677 遗留（双窗口编辑测试） | **BLOCKED_ON_#1677** | SicnuEditLeaseCoordinator 仅存在于 #1677 分支，master 无此符号；在 master 上无法编写针对它的集成测试。等 #1677 合并后由后续波次接手 |
| #1677 遗留（Atlas 导出覆盖恢复） | **BLOCKED_ON_#1677** | 同上：`exportLayout` deliveredFiles 出参与覆盖恢复语义在 #1677 的 layout_service.cpp 修改内；master 上重做=与未合并别轨修复冲突 |

## R17-ui 批次（2026-10-24 新开，动态验真）

| Issue | 分级 | 证据 |
|---|---|---|
| #1678 lab::recordAnswer NaN/Inf | OUT_OF_SCOPE | src/lab/session_state.cpp 不在白名单（src/app|gui|ui|agent/{cartography,mapspec,layout_tools}）→ CROSS_TRACK |
| #1688 bundleBuilderRequest cmd 注入 | OUT_OF_SCOPE | src/teaching_admin/script_adapters.cpp 不在白名单 → CROSS_TRACK |
| #1689 反馈包跨学生泄露 | OUT_OF_SCOPE | src/teaching_admin + src/app/teaching_admin（白名单明确排除）→ CROSS_TRACK |
| #1694 批量评分 checkpoint 摘要 | OUT_OF_SCOPE | src/app/teaching_admin + src/teaching_admin → CROSS_TRACK |

## 历史批次复核（抽验 + 全量清单级）

| Issue | 分级 | 证据 |
|---|---|---|
| #1632（src/app 6 项技术债） | **FIXED_OPEN（6/6）** | ①terrain QFutureWatcher/onAnalysisFinished 已删，defaultOutputPath() 单一派生 ②同左 ③spectral 两处采样均走 `samplingPool()`（:253/:949）④gui_job_adapter 已抽出 `submitWithCallbacks` 共享尾（注释直引 #1632）⑤va_selection_hub clampText 已用 `sicnu::help::truncateSummary` ⑥erase tool `mStampCount <= kMaxStampsPerStroke` 已护 addGeometry |
| #1638（src/gui 3 项） | UNVERIFIED（低优先） | antigravity 桩/REMOVE_ITEM 守卫属 fork 维护债；本轨不实施，记录 |
| #1565 | OUT_OF_SCOPE | 实际缺陷在 src/teaching/autonomy_effective_display.cpp（硬编码 `i<=5`）；src/app 侧 guided_lab_workspace.cpp 已用 clamp(size()-1) |
| #1556/#1558 等 | OUT_OF_SCOPE | src/python、tests test-hygiene（他轨） |
| #1679/#1700 等 R17-data/agent/infra/science 批次 | OUT_OF_SCOPE | 数据/代理/基建/科学轨文件 |

## 结论

第一批判定后，本轨在 master 上可独立实施的入口 = **#1695 第 1 项**（async runner 生命周期，P3 但有真实用户可感知后果：光标卡死 + 悬垂父指针 UB）。其余方向由本轨主动审计补充（见 TASKS.md W2+）。
