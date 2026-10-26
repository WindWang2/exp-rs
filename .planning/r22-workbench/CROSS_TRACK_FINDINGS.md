# R22-05 CROSS_TRACK_FINDINGS（跨轨移交/阻塞记录）

## 移交给其他轨（本轨只读证据，未实施）

| 发现 | 建议归属 | 证据 |
|---|---|---|
| #1678 lab::recordAnswer 接受 NaN/Inf → 会话 round-trip 破坏 | lab/教学轨 | src/lab/session_state.cpp:356-363（Numeric 分支只查 has_value）；src/lab 不在本轨白名单 |
| #1688 bundleBuilderRequest Windows cmd.exe 注入 | teaching_admin/infra 轨 | src/teaching_admin/script_adapters.cpp:381-423 |
| #1689 反馈包跨学生泄露防护未接生产 + 漏报 | teaching 轨 | src/teaching_admin/feedback_pack.cpp:41-73 + src/app/teaching_admin/teaching_admin_dock.cpp:624-630（后者白名单排除） |
| #1694 批量评分 checkpoint 摘要缺规则内容 + 版本写死 ui-1 | teaching 轨 | src/app/teaching_admin/teaching_admin_dock.cpp:394-400 + src/teaching_admin/batch_assessment.cpp:26-44 |
| #1695 第 2/3/5 项（teaching_admin i18n 复数/GUI 阻塞 join、batch_assessment 双文件原子性、lab 空白答案） | teaching/lab 轨 | teaching_admin_dock.cpp:414/:427-432；batch_assessment.cpp:855；src/lab/session_state.cpp:351 |
| #1655 SICNU_CP11_RENDER 门无 CI 归属方（渲染用例在所有自动化车道永久熄灭） | cartography-production 轨 + CI 决策 | tests/test_cartography_production_11.cpp:157-165（该文件在 open PR #1677 文件集内，本轨不写）；.github/workflows/ci.yml 只设 SICNU_MODELS_DIR/-DSICNU_VENDOR_GDAL=OFF。本机 legend paint 挂死（R21-D 记录），本轨不设 =1 强跑 |
| Python 脚本编辑器输出无上限 + 无取消路径（while True: print 卡死 GUI 与内存） | workbench 后续波次（本轨记录，未实施） | src/app/widgets/python_script_editor.cpp:129-137,285-310；m_output QTextEdit 无 setMaximumBlockCount；m_busy 锁 Run 无 Stop |
| temporal 对话框每次编辑在 GUI 线程同步全量 GDALOpen（每交互 ~3×N 次头打开） | workbench 后续波次（RsScanPool 迁移） | temporal_analysis_dialog.cpp:396-431,450-502,555-598 |
| 魔棒 ROI 工具每次点击 GUI 线程 GDALAllRegister+GDALOpen+窗口读 | workbench 后续波次（句柄缓存/离线程） | rs_roi_tool_magicwand.cpp:100-176 |
| CrossSectionWidget 死代码且唯一入口会阻塞 GUI 线程；HistogramWidget::m_cachedDataset 只写 nullptr | workbench 后续波次（删除或先修） | src/app/widgets/cross_section_widget.cpp:30-118；histogram_widget.h:138 |
| #1638 src/gui antigravity 桩 3 项（REMOVE_ITEM 无守卫、12 个 settings nullptr、QgsLayoutHtmlWidget 丢 layout 对象） | gui/fork 维护债（本轨未实施） | src/gui/CMakeLists.txt:1919-1985；antigravity_codeeditor_stubs.cpp:73/585 |

## 本轨 BLOCKED（等 #1677 合并后接手）

- E1 双窗口编辑租约集成测试：`SicnuEditLeaseCoordinator` 仅存在于 #1677 分支。
- E4 Atlas 多页导出“覆盖旧页恢复”原子性：#1677 的 exportLayout/deliveredFiles 语义未合并，master 上重做=与别轨未合并修复冲突。

## 对 R21-D/#1677 的差异核对

- 本轨不触碰 #1677 的 29 个文件；E4 中可独立实施的部分（本轨独占文件）没有发现无需 #1677 符号的前置缺口，故本 PR 不包含 mapspec/layout 生产改动。

## R1-3 附：zh_CN 翻译孤儿条目清单（翻译轨可一次处理）

本轨改动了以下英文源串（resources/translations/sicnu_zh_CN.ts 中旧条目将失效，需 lupdate + 补译）：
1. cartography_dock.cpp: `DPI：` → `DPI:`
2. qgslayoutdesignerdialog.cpp: `Deleted %1 item(s)` → `Deleted 1 item` / `Deleted %1 items`
3. qgslayoutdesignerdialog.cpp: `Duplicated %1 item(s)` → 同上模式
4. qgslayoutdesignerdialog.cpp: `Locked/Unlocked %1 item(s)` → 同上模式
5. qgslayoutdesignerdialog.cpp: `Thematic composition arranged (%1 new component(s)…)` → 同上模式
6. qgslayoutdesignerdialog.cpp: `Select item(s) to duplicate first.` → `Select one or more items to duplicate first.`
7. active_view_host.cpp: 移除确认 `Remove the selected %1 layers…` → 单/复数两分支
