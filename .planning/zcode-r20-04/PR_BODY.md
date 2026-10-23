# hardening(r20-04): Qt/QGIS workbench — GDAL task cancel probe (#1560), CT-6 cartography raw schemas, export DPI floor unification

## BASE / HEAD

- BASE: `28e1891390bb812dc7ef2fe045ac90492c9525f1` (origin/master, 2026-10-21)
- HEAD: `<填入推送时 SHA>`
- Worktree: `exp-rs-agent/r20-04-qt-qgis-cartography-20261022T190613Z`

## 范围（Track 04：Qt/QGIS 工作台 / 交互状态 / MapSpec 制图 / UI 体验）

1. **#1560 [P2] — runGdalTask 真实取消通道**（`src/app/dialogs/raster_processing_dialog_base.{h,cpp}`）
   - task lambda 从不透明 `std::function<QString()>` 扩展为 `std::function<QString(const GdalTaskCancelProbe&)>`，探针绑定 JobEngine 协作取消标志（ctx，engine 在 executor 前装配 job_engine.cpp:922）。
   - 新增 static `gdalCancelProgress`（GDALProgressFunc 适配器）：取消时返回 0 使 GDAL 长操作以 CE_Failure 中止；异常安全（探针抛异常不穿越 C 栈帧）。
   - **范围诚实（R1 #7）**：该 seam 现无生产调用者（对话框均已迁 runOperatorTask，thin-client guardrail tests/test_ui_task_center_contract.cpp:296 保持其关闭）；本修复固化 seam 契约 + 确定性测试 + 可复用 GDAL 适配器，不改变现有对话框的用户可见行为（它们走 runOperatorTask 的既有协作取消）。
2. **CT-6（R19-02 跨轨）— 7 个 cartography 工具 raw schema 规范化**（`src/agent/cartography/cartography_tools.cpp`）
   - 4× 补 `properties:{}`（list_token_sets/list_rules/catalog_index/chart_list）；3× 补 `type:"object"`（diff_templates/export/explain）。
   - registry 腿（raw vs 快照）由确定性红转绿；projection/wire/roundtrip 腿不受影响（快照无需 regen，其钉定的 canonical 形态即契约）。
3. **导出 DPI 下界统一 72**（`src/agent/layout_tools/layout_service.cpp`、`src/app/layout/qgslayoutdesignerdialog.cpp`）
   - layout:export [1,1200]→[72,1200]，与治理链 cartography:export（export.cpp:158/456、produce.cpp:139）一致；GUI 导出对话框 min 36→72；NaN-safe 形式保留（R18-D）。

## RED / GREEN（本地实跑；CI #1491 全仓失效）

- RED（种子 d5b95a9546 = master 源 + 新测试）：`<填入命令与退出码>`
- CT-6 RED 预取证（master 状态二进制）：test_surface_snapshot registry 腿 FAILED @ cartography:chart_list（`{"properties":{},"type":"object"}" == "{"type":"object"}` 歧义即缺项），全套 6/7 过 —— 缺项仅在 raw。
- GREEN ×2（HEAD）：`<填入矩阵>`
- 回归族：`<填入>`
- 变异探针：`<填入>`

## 跨轨

- W1b（属性表绕过 Edit Lease）：只读对照记录于 CROSS_TRACK_FINDINGS #1（main_window lease-aware vs qgsattributetabledialog→facade 直呼），移交数据域联合修复。
- #1632 triage：条目 1/2/5 上游已修（证据在案）；3 不改（测试使用中）；4 移交（提交热路径重构）。

## 独立评审

- R1（fresh reviewer）：APPROVE-WITH-FIXES → 6 项已修（edc7c27e17）、2 项记录不改；reviewer 独立证实 CT-6 完备性（122 工具零 diff）与探针线程安全。
- R2（fresh reviewer，独立于 R1）：`<填入>`

## Known Fail / Not Executed

- `<填入：test_cartography_production_11 环境红区分、CI、Windows/macOS>`

## 验证不等 CI；不自动合并。
