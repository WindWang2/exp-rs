# R19-03 CROSS_TRACK_FINDINGS（跨轨发现，不顺手修）

1. **#1560 — runGdalTask 的取消通道为空（raster_processing_dialog_base.cpp:412-424）**
   - 现状：cancel 回调是带注释的空 lambda；协作取消只在 task() 返回后的检查点生效。长 GDAL I/O 中途取消不即时。
   - 设计方向（seam 注释自述）：把 task lambda 从 `std::function<QString()>` 扩为可拿到取消探针的形式，各对话框在 GDAL 进度回调里轮询。改动面 = 基类签名 + 全部对话框调用点，属独立 WP。
   - 归属建议：本轨后继轮次或 C05 专项。

2. **W1b — qgis_app_facade 编辑路径绕过 Edit Lease（r18-D 遗留，仍未修）**
   - `QgisApp::toggleEditing/saveEdits` 直接 startEditing/commitChanges 不持 DataManager Edit Lease；属性表走此路径。需 facade→shell 回调 + commitEditLease 同步重定位处理，风险高，建议与 data 侧（R15-B 语义后继）联合 WP。

3. **#1638 — src/gui 弱桩三题（本轨禁改 ABI，仅诊断）**
   - (1) `src/gui/CMakeLists.txt:1919+` 34 源/15 头 list(REMOVE_ITEM) 对缺失项静默 no-op，无断言看守；上游升级会静默编回真实实现与桩冲突。建议加"被移除项必须存在"的构建期断言。
   - (2) `antigravity_codeeditor_stubs.cpp:73+` 12 个 QgsSettingsEntry* 静态成员为 nullptr，解引用即段错误。
   - (3) `:585` QgsLayoutHtmlWidget 桩丢弃 frame、setNewItem 恒 true，HTML 图元属性面板静默失效。
   - 归属：弱桩/构建系统 owner（非本轨可写）。

4. **layout:export 与 cartography:export DPI 下限不一致（1 vs 72）**（r18-D 遗留观察，仍未对齐）— 归属 layout owner。

5. **#1655 — SICNU_CP11_RENDER 渲染门在一切自动车道未设置**：本轨以本机探针尝试取证（见 TEST_RESULTS）；车道归属决策留维护者。
