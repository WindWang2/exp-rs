# hardening(r20-04): Qt/QGIS workbench — GDAL task cancel probe (#1560), CT-6 cartography raw schemas, export DPI floor unification

## BASE / HEAD

- BASE: `28e1891390bb812dc7ef2fe045ac90492c9525f1` (origin/master, 2026-10-21, live re-verified before push)
- HEAD: `8d41eecd3c`（6 commits：RED 种子 → 3 修复 → R1 轮 → R2 轮+账本）
- Worktree: `exp-rs-agent/r20-04-qt-qgis-cartography-20261022T190613Z`；Ledger: `.planning/zcode-r20-04/`

## 范围（Track 04：Qt/QGIS 工作台 / 交互状态 / MapSpec 制图 / UI 体验）

1. **#1560 [P2] — runGdalTask 真实取消通道**（`src/app/dialogs/raster_processing_dialog_base.{h,cpp}`）
   - task lambda 从不透明 `std::function<QString()>` 扩展为 `std::function<QString(const GdalTaskCancelProbe&)>`，探针绑定 JobEngine 协作取消标志（engine 在 executor 前 setCancelFlag，job_engine.cpp:922→1097）。
   - 新增 static `gdalCancelProgress`（GDALProgressFunc 适配器）：取消时返回 0 使长 GDAL 操作以 CE_Failure 即时中止；异常安全（探针抛异常不穿越 GDAL C 栈帧）。
   - **范围诚实（R1#7/R2#5）**：该 seam 现无生产调用者（对话框均已迁 runOperatorTask，thin-client guardrail tests/test_ui_task_center_contract.cpp:296 保持其关闭且本轮通过）；本修复固化 seam 契约 + 确定性测试 + 可复用适配器，不改变现有对话框的用户可见行为。
2. **CT-6（R19-02 跨轨遗留）— 7 个 cartography 工具 raw schema 规范化**（`src/agent/cartography/cartography_tools.cpp`）
   - 4× 补 `properties:{}`（list_token_sets/list_rules/catalog_index/chart_list）；3× 补 `type:"object"`（diff_templates/export/explain）。快照/wire/regen 均无改动（canonical 即契约）。
3. **导出 DPI 下界统一 [72,1200]**（`src/agent/layout_tools/layout_service.cpp`、`src/app/layout/qgslayoutdesignerdialog.cpp`）
   - layout:export [1,1200]→[72,1200]（唯一调用者 layout_tools.cpp:835）；GUI 导出对话框 min 36→72；NaN-safe 形式保留（R18-D）。至此四条导出入口（cartography export/atlas、mapspec、layout:export、GUI）一致。

## RED（master 实跑，确定性；种子 d5b95a9546 = master src + 新测试，本工作树构建运行）

| Oracle | 失败证据 |
|---|---|
| #1560 取消不及时 | `REQUIRE(steps.load() < kTotalSteps)` 展开 **`300 (0x12c) < 300 (0x12c)`** —— 取消无法打断，跑完全程 |
| DPI 36 放行 | `CHECK_FALSE(ok)` 展开 **`!true`**；`REQUIRE(!error.isEmpty())` 展开 `false` |
| CT-6 registry 腿 | :322 失败 @ `cartography:chart_list`：`{"properties":{},"type":"object"}` vs `{"type":"object"}`（本工作树 + R20-01 master 状态二进制双取证；全套 6/7 用例过 → 缺项仅在 raw 源） |

## GREEN ×2（HEAD，两轮完整复跑，全部 exit 0）

| 二进制 | 结果（×2） |
|---|---|
| test_raster_processing_dialog_base | 120/120（11 用例）|
| test_surface_snapshot | 17868/17868（7 用例，registry 腿红→绿）|
| test_surface_parity（含 CLI 腿，sicnu_geo_rs_cli 补建后） | 5506/5506（12 用例）|
| test_layout_tools | 1311/1311（14 用例）|
| test_gui_job_adapter / test_m2_batch_a_dialogs | 29/29、132/132 |
| test_mapspec / test_mcp_server | 2830/2830（239 用例）、9956/9956 |
| test_ui_task_center_contract（guardrail） | 169/169 |

构建：全量 70% 处 ld 信号 11（宿主环境症状，同夜 R20-02 同款，非本 diff）→ 定向 10 目标 + CLI 均 EXIT=0；两次 -j1 增量取证构建偏差记录于 TEST_RESULTS（协议 §4 目标增量路径）。

## 跨轨

- **W1b**（属性表绕过 Edit Lease）：只读源码对照（main_window lease-aware vs qgsattributetabledialog→facade 直呼 layer API，qgis_app_facade.cpp:181/238）→ CROSS_TRACK #1，移交数据域联合修复。
- **#1632** triage：条目 1/2/5 上游已修（证据在案）；3 不改（exp_gui 类被 12 处测试引用，"无使用者"说法过期）；4 移交（提交热路径重构）。
- **#1638** src/gui 弱桩：只读诊断（CROSS_TRACK #2）。
- **#1655/CP11**：本机离屏 3/28 环境红与 R19-03 master 记录逐位一致（:811/:1160/:1296），两轮复跑相同；归属决策留维护者。

## 独立评审

- **R1**（fresh reviewer #1）：APPROVE-WITH-FIXES，7 发现（2×P2 测试稳健性、5×P3）→ 6 修 2 记录（edc7c27e17）；reviewer 独立证实 CT-6 完备性（122 工具/36 schema 体 vs 快照零 diff）与探针线程安全。
- **R2**（fresh reviewer #2，独立实例全 diff 重审 + 自跑测试）：**APPROVE**，0×P0/P1/P2，5×P3 → 4 修 1 声明（8d41eecd3c），受影响套件复跑 ×2 全绿。

## Known Fail / Not Executed

- KNOWN_BASELINE_FAIL：test_cartography_production_11 3/28（环境；与本 diff 无渲染路径交集）。
- NOT_EXECUTED：CI（#1491 全仓失效，全部本地验证）；Windows/macOS/sanitizer（本机 Linux offscreen）。

## 不等待 CI；不自动合并。验证不等 GitHub Actions —— 以上全部为本地日志可复核证据。
