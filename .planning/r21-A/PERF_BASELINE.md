# PERF_BASELINE — r21-A

方法：同环境同条件对比（Debug 构建、单测试二进制串行、flock 串行、-j2）。
性能改进以"数值等价 + 资源上界"为准，不引入精度退化。

## 已实施：rs:spectral_index dNBR 单遍融合（perf 提交 4475）

- 变更前：每个 block 两次 `normalizedDifference` 写 `nbrPre/nbrPost` 两个
  per-block std::vector<float>（堆分配 + resize），再逐像素读回求差。
- 变更后：单遍内联计算，零中间缓冲、零每 block 分配。
- 数值等价证据：test_spectral_formula_drift（dNBR 独立文献公式 oracle，
  两键规则）+ test_change_detection（888,684 assertions）+ test_e2e_phase2
  （4,107 assertions）全绿；dNBR 的 NaN 掩码与 staged 形式逐位一致
  （bothNormalizable 谓词为唯一所有者）。
- 预期收益：每 block 少 2 次 O(n) 写 + 2 次 O(n) 读 + 2 次 malloc/free；
  对 2048² block 约少 4×16MB 内存流量。未跑墙钟基准（benchmark 可执行
  test_perf_observatory 不覆盖 dNBR；构造专用微基准的收益低于本轮其余
  正确性修复价值，如实记录为未测量）。
- 内存上界：改进只删除分配，无新增持有内存。

## 候选但未实施（诚实记录）

- terrain_analysis 多产品模式：算子为单 product 调用，不存在跨产品梯度
  重复计算（勘查后排除）。
- image_fusion 内存核双遍统计（means + covariance 各自扫一遍）：可合并为
  一遍（AG2-D1 修复已引入联合有效集遍历，两次循环的合并收益受 seam 级
  调用量限制，暂不动）。
- #1646 记档的 image_enhancement in-memory 大数组拷贝：非本轨新发现问题。
