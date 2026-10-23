# ZCode R20-04 CROSS_TRACK_FINDINGS（跨轨发现，不顺手修）

1. **W1b 复核确认 — 属性表编辑路径绕过 DataManager Edit Lease（live 源码级证据，master 28e1891390）**
   - Lease-aware 路径：`src/app/main_window_vector.cpp:436/:500/:560/:590` — `QgisDesktopWindow::toggleEditing()/saveEdits()` 经 `acquireEditLease/commitEditLease/rollbackEditLease`。
   - 绕过路径：`src/app/qgsattributetabledialog.cpp:299`（saveEdits 信号）、`:706/:711`（保存+级联引用层）、`:930`（toggleEditing）全部走 `QgisApp::instance()` facade；`src/app/qgis_app_facade.cpp:181-207/:238-262` 直接 `layer->commitChanges()/startEditing()/rollBack()`，无任何 DataManager lease 获取/释放。
   - 后果：同一图层的编辑会话经两条 UI 入口进入时，数据面的 lease 记账不一致（属性表编辑会话对 DataManager 不可见，planUnload/卸载规划可能误判"无编辑进行中"）。
   - 处置：本轨只读记录。修复需 facade→shell 回调 + commitEditLease 同步重定位处理（跨数据域），建议 R20-02（data/model/runtime 轨）联合处理；`src/data/**` 为本轨红线。

2. **#1638 弱桩三题（src/gui，本轨只读）** — R19-03 已诊断：(1) src/gui/CMakeLists.txt:1919+ 34 源/15 头 `list(REMOVE_ITEM)` 静默 no-op 无断言；(2) antigravity_codeeditor_stubs.cpp:73+ 12 个 QgsSettingsEntry* nullptr 解引用即段错误；(3) :585 QgsLayoutHtmlWidget 桩丢弃 frame、setNewItem 恒 true。归属 vendored GUI 基线 owner，非本轨可写。复核 master 现码仍如此（本轮未逐一重测，静态核对）。

3. **#1655 / test_cartography_production_11 渲染门** — R19-03 TEST_RESULTS 记录本机离屏 3/28 失败（atlas coverage layer / e2e sha / corpus）。本轨构建完成后以相同门复跑区分环境红与回归（SICNU_CP11_RENDER 语义 #1655 已默认运行）。

4. **merge_loss_audit.py 历史告警** — 检测器含全史 540 merges/224 findings，多数已由 r18/r19 各轨消化；本轨 push 前会重跑并逐条比对 parent/base/现码，只处理与本轨 diff 相交者（协议 §9.4）。
