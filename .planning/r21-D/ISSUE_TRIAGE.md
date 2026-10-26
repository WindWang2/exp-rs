# R21-D ISSUE_TRIAGE（live 源码实证，基线 911e0b17a3，2026-10-24）

分诊类别：OPEN-UNFIXED / FIXED-OPEN（已修未关，需复测证据）/ PARTIAL / OBSOLETE / DUPLICATE / NEEDS-EVIDENCE / OUT-OF-SCOPE。

## 本轨白名单内（src/app 除 teaching、src/gui、src/ui、src/agent/cartography、src/agent/layout_tools、resources UI、对应测试、docs 工作台/制图）

| Issue | 类别 | 现码证据（file:line） | 处置 |
|---|---|---|---|
| #1535 P1 算法 runner feedback UAF | FIXED-OPEN | src/app/dialogs/async_algorithm_runner.cpp:50-58（feedback 所有权移交 m_task + 注释）；析构 :20-31 | 复测（现有测试）后建议关闭；不重复修 |
| #1536 P1 guided workflow 行号换算 | FIXED-OPEN | src/app/widgets/guided_workflow_widget.cpp:156-181（kLoadErrorEntryRows=1，注释引 #1536） | 复测后建议关闭 |
| #1537 P1 STAC searchNext 旧分页 | FIXED-OPEN | src/app/stac_client.cpp:225-231（lastPageGeneration 守卫）+ deliverPage :283/:289（代数写入） | 复测后建议关闭 |
| #1559 P2 workspaceService seam 悬空 | FIXED-OPEN | src/app/main_window_docks.cpp:747-769（destroyed 连接清理，注释引 #1559） | 复测后建议关闭 |
| #1560 P2 runGdalTask 取消 hook 空 | FIXED（R20-04, PR #1672） | raster_processing_dialog_base.cpp:43/:84-91/:381-388 | 回归保护（本轮跑套件） |
| #1561 P2 光谱剖面 GUI 线程同步 GDALOpen | FIXED-OPEN | spectral_profile_widget.cpp:171-233（网络源 QtConcurrent 采样池 + generation）；本地源同步缓存路径保留 | 复测后建议关闭 |
| #1562 P2 log_panel Level 过滤空 lambda | FIXED-OPEN | log_panel.cpp:192（refilter 连接）/:181（可见计数）/:201-210（shouldShowMessage） | 复测后建议关闭 |
| #1563 P2 精度报表 CSV 注入/畸形 | FIXED-OPEN | rs_accuracy_panel.cpp:26-43（RFC4180 引号转义） | 复测后建议关闭 |
| #1564 P2 VaCursorProbe 节流绕过 | FIXED-OPEN | va_cursor_probe.cpp:74-86（startPending 节流 + QTimer 重臂） | 复测后建议关闭 |
| W1b（非 issue，R19-03→R20-04 移交）属性表绕过 Edit Lease | **OPEN-UNFIXED** | qgis_app_facade.cpp:181-287（saveEdits/toggleEditing 直连 commitChanges/startEditing/rollBack，零 lease）；qgsattributetabledialog.cpp:299/:706/:711/:930 全经 facade；对照 main_window_vector.cpp:766-880 lease 路径 | **本轨修复主线** |
| #1621 P2 恒真断言（classification_studio/m2_batch_a_dialogs） | OPEN-UNFIXED（测试域） | tests/test_classification_studio_widget.cpp:166/:221-227；tests/test_m2_batch_a_dialogs.cpp:126-129/:153-156/:180-183 | 本轨修复（UI 测试白名单） |
| #1622 P2 墙钟性能门槛（spectral_profile_widget 测试） | OPEN-UNFIXED（测试域） | tests/test_spectral_profile_widget.cpp:472 | 本轨修复 |
| #1623 P2 sicnu_zh_CN.qm 无 install 规则 | OPEN-UNFIXED（共享灰区） | 根 CMakeLists.txt D6 i18n 管线（grep 无 install(QM)） | 本轨 append-only 修复 |
| #1632 条目4 gui_job_adapter 三重 submit | OPEN-UNFIXED（P3） | 待定位（gui_job_adapter.cpp） | 评估后小步重构或记录 |
| #1638 P3 src/gui 弱桩三题（REMOVE_ITEM no-op / nullptr 解引用 / layout html 桩） | NEEDS-EVIDENCE | src/gui/CMakeLists.txt:1919+；antigravity_codeeditor_stubs.cpp:73+/:585 | 评估：vendored 基线，修复风险 vs 收益 |
| #1619 P2 309 weak 桩 void f(void) ABI | NEEDS-EVIDENCE | src/gui/weak stub 生成物 | 同上（机械量大，评估范围） |
| #1620 P2 qgssettingsregistrygui.cpp 替换 | NEEDS-EVIDENCE | src/gui/qgssettingsregistrygui.cpp | 同上 |
| #1655 P3 CP11 渲染门（3/28 离屏红） | NEEDS-EVIDENCE | tests/test_cartography_production_11.cpp:811/:1160/:1296 | 构建完成后同门复跑分类环境红/产品红 |

## OUT-OF-SCOPE（其他轨区域，不修；跨轨发现记录 CROSS_TRACK_FINDINGS.md）

- #1538/#1541-#1546/#1550-#1556 等算法/运行时/数据/SDK/Python 域 P1——各归其轨。
- #1557 src/native macOS 通知 UAF——native 轨。
- #1558 tests/test_e2e_open_issues.cpp:817/:871 恒真（#784/#799 处理域）——处理/验证轨；记录 CROSS_TRACK。
- #1565 src/app/teaching/guided_lab_workspace.cpp——教学轨（D 白名单显式排除 teaching 子目录）。
- #1617/#1618 src/native——native 轨。
- #1624-#1658 各模块技术债汇总（P3，多数一句话索引）——按目录归属各轨；其中 #1655/#1657/#1658 涉测试文件，#1655 归本轨评估，其余评估后记录。

## 126 条全量的其余部分

属 src/analysis、src/processing、src/runtime、src/data、src/dataset、src/geospatial、src/agent(非 cartography/layout_tools)、src/teaching*、src/verification、src/cli、src/workflow、src/help、src/faultlab、src/explain、src/experiment、src/science_context、src/preflight、src/plugins、src/python、src/lab_pack、src/verify*、src/operators、build 脚本等——不在 D 白名单，不动。
