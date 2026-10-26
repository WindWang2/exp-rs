# R22-03 Issue Triage（动态验真，不因 OPEN 就重复修）

基线：911e0b17a3533ca0d79c1b68b000d6052c4c3c61。每项均有源码证据。

## #1706 [P1] FeatherAccumulator 多槽位共享 cv::Mat + 权重重复累加 — **OPEN_UNFIXED**（本轨 C0 主工作包）

证据（@911e0b17 实读源码，与 issue 行号逐一吻合）：
- `src/operators/runtime/tile_inference_engine.cpp:828-829` `resetWindow()`：
  `m_sum.assign( m_slots, cv::Mat::zeros( m_span, m_rasterW, CV_32FC1 ) )` —— `cv::Mat` 拷贝为浅拷贝（引用计数），
  `assign(n, v)` 对同一 Mat 拷贝构造 n 次 → n 个槽位共享同一数据缓冲。
- `:780` `add()`：`weightRow[x] += w;` —— 每槽位调用一次。
- 调用点（每 tile 每 slot 一次）：`:1839`（probability，`bandOffset + c` 按类别循环）、`:1881`（derived，按类别循环）、
  `:2015`（uncertainty 槽）。→ 权重被累加 C 次。
- 症状推导与 issue 一致：probability 模式每波段 = mean(所有类别)（(Σ v_c·w)/(C·w)），全部波段相同；
  argmax 全部相等 → 全图 class 0；uncertainty 槽同污染。
- issue 附带独立 OpenCV 小程序复现（三槽位 data 指针相同）。现有 `tests/test_model_runtime_9.cpp` feather 用例
  （M5 :737 单通道、#r18B :1028 单通道）均 C=1，无法暴露。
- 排重：#1669/#1659/#1508/#1673 均未触及 FeatherAccumulator 的槽位分配与权重累计逻辑
  （#1673 只加 mask_threshold 的 head-role 门控；git log -S "m_sum.assign" 无修复提交）。

分级：**OPEN_UNFIXED，P1 正常输入下结果损坏** → 进入实施（C0）。

## #1554 [P1] ModelSessionPool 析构不持锁遍历 sessions — **FIXED_OPEN**（不重复修）

证据：`src/runtime/gpu/gpu_plane.cpp:44-64`（@911e0b17）析构函数已在 `std::lock_guard<std::mutex>` 下遍历
`sessions` 并 freeVram，guard 作用域在 `delete m_impl` 之前结束（避免已析构 Impl 内解锁 UB）。
修复提交：`d9f1b886f1 fix(src/runtime): resolve full-review findings #1553 #1554 #1603 #1604 #1605`
（`git merge-base --is-ancestor d9f1b886f1 origin/master` → 在 master 内）。
issue 状态未关（OPEN），属 FIXED_OPEN。本轨不重复实现；PR body 中说明可关闭。

## #1553 [P1] ChunkGraph::addSink 绕过单消费者守卫 — **FIXED_OPEN**（不重复修）

证据：`src/runtime/chunk/chunk_graph.cpp:92-104`（@911e0b17）`addSink` 已调用
`requireFreeConsumerLocked(input)`（同 input 替换 sink 合法除外），双向 build 顺序均被守卫。
修复提交同为 `d9f1b886f1`。FIXED_OPEN，不重复实现。

## 近期 R21-B 运行时遗留风险复核

- #1673（open）对 `tile_inference_engine.cpp` 的改动为 `headRoleIsThresholdable()` 门控 mask_threshold
  （risk B5），不涉及 FeatherAccumulator/权重/槽位 —— 与 C0 无语义重叠，但有同文件 merge 冲突风险
  （见 PR_COLLISION_MATRIX.md）。
- #1673 的 `resolvedHeadNames[h]` 越界修复（R2-F02）位于 runMultiInput；本轨不动 runMultiInput。

## 其余候选（本轨范围内扫描，暂不进第一批）

- #1541（检测 lane CV_CN_MAX 护栏）：`9b42bc93f7` 已宣称修复（#1541 在该提交标题中），验真后在 CROSS_TRACK/FINAL_REPORT 标注，不重复修。
- #1575/#1576/#1577（provider/batch/显存估算）：多在 #1659/#1669 已处理或属低优先增量；本轨不抢。
