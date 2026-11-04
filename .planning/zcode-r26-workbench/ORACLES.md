# ORACLES — R26-02 workbench（每条 RED→GREEN×2）

## WP1 分类类表 UAF（P1）
- Oracle: 双击类表色块改色后，classDefEdited 携带的原类名与改前一致（UAF 下为垃圾/空），且类定义颜色已更新。修复：先拷贝 nameItem->text() 再触发 setClassDef。
- 测试: tests/test_r26_workbench_classification_ux.cpp "class table color edit keeps name stable"

## WP2 合并类状态闭环（P1）
- Oracle: runMergeClasses 后 (a) m_rois->classDefs() 不再包含源类 id；(b) sample layer 中源类要素 cls_id 全部改写为 target；(c) 类表行数 = 合并后类数。
- 测试: 同文件 "merge classes removes source defs and relabels samples"

## WP3 生命周期（P2）
- Oracle A: openSourceRaster 换图时若已有样本，弹确认框；拒绝则样本保留。
- Oracle B: dirty 且 0 ROI 时关窗：不再出现"Save 后窗口关不掉"死锁（问题降级为 Discard/Cancel 二选，或 Save 走"无可导出"仍允许关闭——采用前者）。
- Oracle C: 运行中（hasInFlightCompute）关窗：弹确认（Cancel 优先），不再静默杀任务。
- 测试: 同文件三个 case。

## WP4 错误提示精确性（P2/P3）
- Oracle A: 波段越界（3 波段图输入 9）时状态栏显示 res.errorMessage（"Band ... out of range"）而非"Not enough training samples"。
- Oracle B: applyWorkflowSnapshot 对非法 classifierKind 回退默认（NormalBayes/上一次合法值），setup bar 三按钮必有其一选中。
- 测试: 同文件 "band error surfaced precisely" + "invalid classifier kind falls back"

## WP5 谱线 NoData（P1）
- Oracle: 带 NoData=-999 的栅格上点击 NoData 像素，values()[b] 为 NaN（哨兵不再计入 min/max）；无 NoData 元数据行为不变。
- 测试: tests/test_r26_workbench_spectral_ux.cpp "nodata sentinel becomes NaN in profile"

## WP6 ROI 统计（P1/P2）
- Oracle A: compute → setRasterLayer(另一层) 后泵事件，Refresh 按钮恢复可用（修复前永久禁用）。
- Oracle B: 全 NoData ROI 的统计表格显示 "—"（非 0.00），摘要带 "no valid pixels"。
- 测试: tests/test_r26_workbench_roi_histogram.cpp

## WP7 直方图/波长（P1/P2）
- Oracle A: setRasterLayer 后异步直方图落地时发 bandDataReady，stretch widget 把 min/max spin 重锚到真实数据范围（16-bit [1000,4000] → spin=1000/4000，修复前 0/255）。
- Oracle B: 降序波长（如 1650,830,490）时谱线 X 轴取 min/max，曲线铺满图表（修复前塌缩左缘）。
- Oracle C: setBand 越界（99）不改变当前波段数据/标题错位——拒绝并置错误态。
- 测试: roi_histogram + spectral_ux 两个文件。

## WP8 对话框一致性（P2/P3）
- Oracle: ChangeDetectionDialog 失败/取消/成功后 m_statusLabel 不再停留 "Running..."（cleanupRunResources 虚挂点复位；成功后 onResult 覆盖统计文本）。
- 测试: tests/test_r26_workbench_dialogs.cpp "status label leaves Running state"

## WP9 引导工作流诚实完成（P2）
- Oracle A: ensureOutputDirs 失败时不再提交任务（submitJob 计数 0）。
- Oracle B: 步骤运行失败后 Next 被阻止并提示；全部跑完才发 workflowCompleted；失败步骤不发射 stepCompleted 的"成功"语义（导航允许，但 workflowCompleted 只有零失败才发）。
- 测试: tests/test_r26_workbench_guided.cpp

## WP10 帮助极端预算（P2 残留）
- Oracle: HelpCompact::toJsonText 在 budget∈{0,1,5,8,9} 输出始终可被 QJsonDocument 解析（"{}" 兜底）。
- 测试: tests/test_r26_workbench_help.cpp

## E2E
- 至少一条穿越真实生产注册算子路径的 GUI 链（guided workflow 或分类 preview 走 TaskCenter→rs: 算子→结果）。
