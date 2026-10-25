# R22-01 science Oracle 定义

## Oracle 1（#1707 距离变换）
- RED：新增用例对 3×4 复例（rows 010/001/000/000）断言 (0,3) 处平方距离=8（欧氏 √8），
  旧实现返回 √6 → FAIL。
- GREEN：修复后 PASS。
- 强化：≥3000 随机 mask（含不同宽高、稀疏/稠密前景）与暴力 O(N²) 逐点最近邻对拍；
  断言经 float 往返取 sqrt 后 margin=1e-4 比较（缺陷量级为平方距离差 ≥2 个单位，
  margin 远小于任何真实缺陷）。

## Oracle 2（#1684 方差）
- RED：构造 AccumulatorStats（N=1e6，值 10000.3±0.5 交替），断言 stddev≈0.5（误差 <1%）。
  旧实现（float 均值）给出 ~0.58（+17%）→ FAIL。
- 附：常量序列（σ=0 精确）、N=1、大偏移小方差（1e6±0.25）、双精度高精度参考对比。
- 联动回归：OBIA segment features / 统计工具现有测试保持绿。

## Oracle 3（#1685 确定性随机）
- 新原语 golden 序列测试：固定 seed 的 Pcg32/mt19937+固定算法输出序列以硬编码
  十六进制/十进制期望值写入测试（跨 STL 可复现的锚点）。
- Fisher-Yates 为固定算法（同 rs_cross_validation 既有纪律）；
  Box-Muller 正态为固定两均匀 draw 的封闭公式。
- 调用点等价性：split/kmeans/PPI/RANSAC 现有功能测试保持绿（序列值会变，属预期，
  因为旧值本就不可移植；不得有测试断言依赖旧 libstdc++ 序列的精确值——逐一核查）。

## Oracle 4（#1686 重采样）
- RED：4×4→8×8 同范围上采样，断言输出四角/边缘非 NoData（旧实现边缘 NoData）→ FAIL。
- GREEN：边缘值等于 clamp 采样参考；亚像元平移、旋转不丢有效值；
  明确超出 [-0.5, w-0.5] 足迹的点仍 NoData。

## 总 Oracle（交付门）
1. 以上全部 RED→GREEN，关键测试双跑一致。
2. 基线既有测试族（primitives5/math_utils/resampler/rs_operators/kmeans/endmember/
   classification split/obia segmentation）无回归。
3. R1/R2 独立 review 的 P0/P1/P2 全部闭环。
4. GitHub PR 创建成功（真实 URL）。
