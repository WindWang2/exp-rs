# ISSUE TRIAGE — 历史风险 issue 在最新 origin/master (f316dfdbb4) 上的复验

方法：不按旧 line number 机械修；在最新 master 重新定位代码，逐项给出
`still-valid / fixed-by-later-merge / changed-root-cause / duplicate /
cannot-reproduce / out-of-scope` 分类 + 证据 + 后续动作。

## #849 SelectionContext::computeSnapshot heap UAF（address reuse）

- 分类：**fixed-by-later-merge**（`8f6293bceb` 系列）。
- 证据：`src/app/workbench/selection_context.cpp` `dying` lambda 现为：
  - `d.guard && d.guard.data() == layer` → doomed-but-alive，过滤；
  - `!d.guard && d.raw == layer` → 先验证
    `QgsProject::instance()->mapLayers().values().contains(layer)`（地址复用的
    合法新图层放行），否则视为 destroyed 永不复现；
  - **不再对未验证 raw 指针调用任何成员函数**（原 UAF 点 `layer->id()` 已消失）。
  - 另有 tombstone 退休逻辑（`refreshNow`，canvas 仍可能产生悬垂指针期间保留）。
- 回归测试：`tests/test_selection_context.cpp` TEST_CASE "#849"（删除后 computeSnapshot
  + 地址复用两个阶段）→ 维持。
- 动作：无新修复；保持测试绿。

## #857 QgsLayerTreeMapCanvasBridge 泄漏（removeView）

- 分类：**fixed-by-later-merge**。
- 证据：`src/app/display/qgis_display_manager.cpp`：
  - `removeView` 显式 `delete viewIt->second->bridge.data(); bridge = nullptr;`（:975-977）；
  - record 清理路径同样 delete（:217-218）；record 持有 `QPointer` + 显式 delete，
    无 double-delete（delete 后立即置空）。
- 回归测试：`tests/test_qgis_display_manager.cpp` removeView 系列用例 → 维持。
- 动作：M3 做窗口销毁顺序的补充 teardown 验证（无新 owner 变更）。

## #859 canvas 异步 teardown race（~QgisDesktopWindow / project clear）

- 分类：**fixed-by-later-merge**。
- 证据：`main_window.cpp:312`、`main_window_project.cpp:41,102` 均改用
  `m_mapCanvas->stopRenderingAndSettle()`（阻塞等待渲染 job 结束再清 layer）。
- 动作：M0 以既有 shutdown policy 测试 + project clear/switch stress 保持验证。

## #861 RsScanPool 全局 generation 干扰 → widget 永久 Computing

- 分类：**fixed-by-later-merge**（按新根因修：per-owner generation）。
- 证据：`rs_scan_pool.h` 增加 `nextGeneration(owner)` / `cancel(gen, owner)` /
  `isStale(gen, owner)` 的 per-owner 通道（`m_ownerActiveGeneration`）；
  `roi_statistics_widget.cpp:177` 以 `this` 为 owner 开 generation；stale 早退
  经 `QMetaObject::invokeMethod(qApp, ...)` + QPointer + epoch 三重保护回 GUI 线程
  复位 `m_computing`。
- 残留观察（非 #861 本体）：`histogram_widget.cpp` stale-exit 同样安全（superseded
  时新请求负责完成态）；但 `GDALOpen` 失败路径静默 return，不向 UI 上报错误
  （F3，P2，M0 顺手修：错误也要 marshal 回 UI 呈现，与 roi 一致）。
- 回归测试：`tests/test_scan_pool.cpp`（supersede / targeted cancel）→ 维持并补充
  per-owner 不干扰用例（若缺）。

## #882 empty-state CTA / shortcut mismatch

- 分类：**still-valid（部分残留）**。
- 已修：welcome 画布文案改为「导入数据 / Ctrl+O 打开已有工程」（与 `project.open`
  = `QKeySequence::Open` 一致）；Data Manager 与 layer dock 的 empty-state CTA
  均已接 `importLayer()`。
- **残留（本方向 F2）**：`src/app/workflow/pipeline_editor_dock.cpp:52-59` 工具栏
  tooltip 声称 "(Ctrl+N)"、"(Ctrl+O)"、"(Ctrl+S)" 指向工作流新建/打开/保存，但这些
  QAction 从未 setShortcut；真实 Ctrl+N/O/S 由 `project.new/open/save` 持有
  （且菜单裸 action 与 registry 双重登记，见 F1）。用户按 tooltip 提示按键会打开
  **工程**而非工作流——#882 同类 drift，issue 关闭时未覆盖此文件。
- 动作：**M2 修复**：workflow 命令进入 CommandRegistry（真实 command id +
  不与 project.* 冲突的 shortcut 或无 shortcut + 文案不撒谎），菜单/工具栏经
  registry projection 消费。

## F1（baseline 新发现，非既有 issue）：project.* shortcut 双权威

- 分类：**still-valid**（架构债，M2 主体）。
- 证据：`main_window_menus.cpp:118-126` 裸 `addAction(..., QKeySequence::New/Open/Save)`
  直连 `QgisDesktopWindow` slots；`command_defs.cpp:45-70` registry 登记同 id 语义、
  同 shortcut、handler 转发到同一 slots。两条平行通路、shortcut 实际持有者不明确。
- 动作：**M2**：菜单 host 改为消费 `registry->action(id, installShortcut)` projection，
  使 "exactly ONE projection installs the shortcut" 规则贯穿 shell。

## 结论

五个历史 issue：4 个 fixed-by-later-merge（验证 + 测试保持），1 个（#882）
still-valid 残留 → M2。F1/F3 为 baseline 新发现 → M2/M0。
