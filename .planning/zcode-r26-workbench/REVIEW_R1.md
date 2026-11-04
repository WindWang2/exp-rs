# REVIEW R1 — 独立评审（agent 无实现参与，2026-10-31）

判定：需修复后可合并。Findings 与处理：

| ID | 严重度 | 内容 | 处理 |
|---|---|---|---|
| P1-1 | P1 | cleanupRunResources 在成功路径于 onResult 之后运行，无条件覆写统计文本 | ✅ 已修：仅当仍为 "Running..." 时复位 + 成功 SECTION 补断言 |
| P1-2 | P1 | help 测试 budget 0/1 断言 size<=budget 不可满足（"{}"=2字符） | ✅ 已修：0/1 改为断言 =="{}" 且可解析；2/5/8/9 保留 size 上限 |
| P2-1 | P2 | 分段线性控制点编辑未置 m_spinsTouched，迟到 landing 会 reset 用户点集 | ✅ 已修：onPiecewisePointsChanged 置 touched（信号仅鼠标路径发出，resetPiecewisePoints 不发） |
| P2-2 | P2 | sample layer 本已可编辑时合并重标注不触发 rebuild（stale m_rois） | ✅ 已修：合并后显式 rebuildRoisFromSampleLayer() |
| P2-3 | P2 | NoData 双 double 精确比较漏判 float 哨兵（如 3.7） | ✅ 已修：isNoDataPixel 同时比较 float round-trip（对齐 roi_statistics 既有做法） |
| P3-1 | P3 | setBand 拒绝后陈旧 scanError 不清除 | ✅ 已修：接受分支清空 |
| P3-2 | P3 | setRasterLayer 未复位 summary 标签（"Computing…" 滞留） | ✅ 已修 |
| P3-3 | P3 | 单波段读失败仍以 0.00 入表 | ✅ 已修：skipped band 置 NaN |
| P3-4 | P3 | ROI 测试按第一个子按钮找 Refresh（false-pass 风险） | ✅ 已修：refreshBtn 加 objectName + 按名查找 |
| P3-5 | P3 | guided 测试在仓库根建 output/labs/lab98_r26_probe（gitignored） | ✅ 已修：测试 pin SICNU_DATA_DIR + chdir 到临时数据根；复跑全绿且仓库不再生成 output/ |
| P3-6 | P3 | tests/CMakeLists.txt EOF 尾部冲突（兄弟 PR 同为 append-only） | 记录于 PR 描述（机械解决） |
RED 有效性：R1 逐条核查全部新测试的 pre-fix 失败性，均判定有效（help 测试除外，已修）。
