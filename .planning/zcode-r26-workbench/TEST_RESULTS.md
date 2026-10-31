# TEST RESULTS — R26-02 workbench（全本地、QT_QPA_PLATFORM=offscreen）

## 新测试（tests/test_r26_workbench_*.cpp，6 目标 16 用例 154 断言）
### RED 轮（pre-fix 生产树 + 中立测试缝；stash commit 9d62a746 恢复）
| 目标 | RED 结果 |
|---|---|
| test_r26_workbench_spectral_ux | 1 case 失败（NoData 哨兵未转 NaN：2 断言失败）|
| test_r26_workbench_roi_histogram | 4 cases 全失败（按钮死锁/全 NoData 0.00/spin 0-255/setBand 99 接受：8 断言失败）|
| test_r26_workbench_dialogs | 1 case 失败（失败路径标签残留 "Running..."：1 断言失败）|
| test_r26_workbench_guided | 2 cases 全失败（doomed task 仍提交→"failed:"；workflowCompleted 误发：4 断言失败）|
| test_r26_workbench_classification_ux | 6/7 cases 失败（UAF 快照缺失/合并未闭环/无换图确认/0-ROI 死锁关窗/波段误报/kind 无回退；源码扫描 case 是确定性 RED；switching case pre-fix 挂起→watchdog 20s 后判定失败）|
| test_r26_workbench_help | 未参与 RED（master 已修复，属残留覆盖；GREEN 全过）|

### GREEN 轮 1 + 轮 2（修复后，连续两轮）
spectral_ux 12/12 · roi_histogram 33/33 · dialogs 25/25 · guided 24/24 · classification_ux 46/46 · help 14/14 —— **两轮均全绿**

## 受影响既有测试回归（修复后）
test_roi_statistics_widget 29/29 · test_spectral_profile_widget 154/154 · test_histogram_stretch_widget 108/108 ·
test_change_detection_dialog 127/127 · test_guided_workflow_widget 124/124 · test_classification_window 33/33 · test_help_core 2173/2173 —— **全绿**

## 注册完整性（当前工作树，含未提交新测试）
integrity: 1063 registered targets + 50 add_test names（base origin/master: 1063/50; lost: 0+0; added: 0+0）；0 marker finding(s)；0 duplicate(s)
注：新测试目标尚未 commit，脚本按 HEAD 统计；commit 后复跑预期 1069 目标（+6）。

## 构建
- configure: Debug + ENABLE_TESTS=ON，flock 宿主锁，--parallel 4（未超 -j4）
- 全部 6 新目标 + 7 回归目标编译链接 0 error
- 编译产物：build-r26-workbench/tests/test_r26_workbench_*

## E2E（真实生产路径）
- test_r26_workbench_dialogs 成功 SECTION：真实构造 QgsRasterLayer×2 → ChangeDetectionDialog → 点击 Run →
  rs:change_detection 经 runOperatorTask→TaskCenter→真实算子→GTiff 输出落盘（QFileInfo::exists 断言）→ 标签离开 Running 且统计保留
- test_r26_workbench_classification_ux merge case：真实 QgsClassificationMainWindow + startPostProcessTask
  真实 TaskCenter 任务完成 → onMergeClassesRequested → RsMergeClassesDialog accept → recode 任务提交
- test_r26_workbench_guided 两 case：真实 LabSpec 加载 + 真实 JobRequest 提交（失败算子）+ 完成门判定

## 未运行/平台缺口
- Windows 机房离线 bundle/脚本：NOT_EXECUTED（无 Windows 真机）
- GPU / 远程模型推理：NOT_EXECUTED（本轨不触碰推理）
- 人工视觉/高 DPI 截图对比：NOT_EXECUTED（无显示环境；offscreen 仅验证行为）
