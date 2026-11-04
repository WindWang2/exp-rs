## 范围与基线

- base: `master` @ `2610cb4741e69a89124921e44c03e340e2522ee2`（fetch 时确认无更新）
- head: `agent/r26-workbench-20261030T151124Z`
- 任务：R26-02 Qt/QGIS 遥感工作台 + 本科实验交互（`workbench` 轨）
- 冲突规避：写区仅 `src/app/classification/**`、`src/app/widgets/**`、`src/app/dialogs/**`（不在 #1724-#1728 changed-files 中的具体对话框）、`src/help/**`（仅测试）、`tests/test_r26_workbench_*.cpp` + `tests/CMakeLists.txt` 尾部追加块。与 5 个开放 PR 的源码文件零重叠。

## 功能变化（事实性）

分类 Studio：
- 类表双击换色不再对已被 `rebuild()` 释放的 `QTableWidgetItem` 读 `text()`（heap-use-after-free，`rs_class_table_widget.cpp`）
- 合并类：提交前 busy 守卫；sample layer 源类 `cls_id` 全部重标注为 target；`setClassDefs` 单发替换删除源类定义；显式 `rebuildRoisFromSampleLayer()` 覆盖"图层本已可编辑"路径——合并后类表/样本/后续 Apply 不再与 recode 结果脱钩
- 打开新影像前对已有样本弹确认（拒绝则保留）
- 关窗：在途任务先确认；0-ROI dirty 从"Save 必失败且窗口关不掉"改为 discard/cancel 二选
- `buildTrainingData` 出参带 `res.errorMessage`，Apply/Preview/CV 三处显示真实病因（如 `Band 9 out of range (1-3)`）而非统一的"样本不足"；`setCurrentKind` 对恢复出的非法枚举回退默认

光谱/可视分析：
- 谱线两条采样路径（inline + worker）把每波段 NoData 哨兵转 NaN（比较含 float round-trip），不再把 -9999/0 画成真实数据并压平 Y 轴
- ROI 统计：计算中切换图层后 Refresh 按钮不再永久禁用；全 NoData/空 ROI 以 NaN→"—"入表与摘要明示，不再显示合法 0.00；CSV 固定 C locale；空统计导出有提示；读失败 band 置 NaN；`setRasterLayer` 复位摘要
- 直方图：`setBand` 越界拒绝（不再保留旧数据配新标题）；新增 `bandDataReady` 信号，HistogramStretchWidget 在扫描落地后把 min/max spin 重锚到真实物理范围（16-bit 不再停在 0/255）；用户手动调整（含分段点）后迟到落地不再覆写

对话框与引导：
- ChangeDetectionDialog：失败/取消/成功无统计路径都不再残留 "Running..."（`cleanupRunResources` 仅抬 Running 态，不覆写成功统计）
- GuidedWorkflowWidget：输出目录创建失败中止提交（不再二次报错）；失败步骤加入完成门，`workflowCompleted` 仅零失败时发射；新增 `loadWorkflowsFromDir` 测试缝与按钮 objectName

帮助：
- 为 #1614 的残留缺口补极端预算（0/1/2/5/8/9）可解析性回归测试

## Oracle 与验证（本地，offscreen）

- 6 个新目标 16 用例 154 断言：RED（pre-fix 树，见 TEST_RESULTS 明细）→ GREEN 连续两轮全绿
- 受影响既有测试 7 目标 2838 断言全绿：`test_roi_statistics_widget`、`test_spectral_profile_widget`、`test_histogram_stretch_widget`、`test_change_detection_dialog`、`test_guided_workflow_widget`、`test_classification_window`、`test_help_core`
- E2E（真实生产路径，非 mock）：ChangeDetectionDialog Run → `runOperatorTask` → TaskCenter → `rs:change_detection` 真实算子 → GTiff 落盘；分类 merge 走真实 post-process TaskCenter 任务 + 真实 RsMergeClassesDialog；guided 走真实 JobRequest 提交与完成门
- 构建：Debug + ENABLE_TESTS=ON，宿主构建锁，`--parallel 4`（未超 -j4）；0 error
- `git diff --check` 干净；`scripts/check_merge_registration_integrity.py --base origin/master --require-refs` 0 lost / 0 duplicate
- R1 独立评审（未参与实现）11 项 findings 全部处理（P1×2、P2×3、P3×6）；R2 独立复审见 `.planning/zcode-r26-workbench/REVIEW_R2.md`

## 未测试矩阵（如实标注）

- Windows 机房离线 bundle/脚本审计：`NOT_EXECUTED`（无 Windows 真机）
- GPU / 远程模型推理：`NOT_EXECUTED`（本轨不触碰推理路径）
- 高 DPI / Light-Dark QSS 截图对比：`NOT_EXECUTED`（无显示环境，offscreen 仅验证行为）
- 分类 Studio 部分人工交互（魔棒容差 UI、ROI 点/矩形工具接入）仅做源码级判定，未做真实鼠标操作验证

## 跨轨依赖与冲突说明

- `tests/CMakeLists.txt`：仅尾部 `# BEGIN R26-workbench` … `# END R26-workbench` 追加块；5 个开放 PR 同样在 EOF 追加，逐个合并时尾部文本冲突属机械可解（append-only）
- 不依赖 #1726 的 import preflight API（数据导入可理解性留待其合并后）；不触碰 #1724/#1725 的 shell/teaching_admin/lab/spec_runtime 文件
- 教学引导只读 LabSpec 数据层，未创建平行评分权威

## 规划账本

`.planning/zcode-r26-workbench/`：GOAL / BASELINE / OWNERSHIP / ISSUE_TRIAGE / TASKS / ORACLES / LOOP_LEDGER / TEST_RESULTS / PERF_BASELINE / REVIEW_R1 / REVIEW_R2 / FINAL_REPORT
