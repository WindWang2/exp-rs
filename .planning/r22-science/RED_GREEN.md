# R22-01 science RED→GREEN 证据

环境：worktree `agent/r22-science-20261025T133250Z`，基线 master@911e0b17，
build `build-r22-science`（Debug），宿主 GDAL/Qt 环境同 R21-A（见 BASELINE.md）。
运行命令：`/tmp/r22s-run.sh <binary>`（等价 R21-A run-tests.sh 环境）。

## RED（旧实现，未修复源码，2026-10-26T00:3x）

| 测试 | 结果 | 关键失败 |
|---|---|---|
| test_r22_science_numerics（7 例） | 4 FAIL / 3 PASS | #1707 反例 (0,3) 得 2.449(√6) 期望 2.828(√8)（L78）；随机对拍首错（L161）；#1684 10000.3±0.5 stddev 失败（L125）；#1684 1e6±0.25 stddev=64.0 期望 0.25（L206，偏差 25600%） |
| test_r22_science_resampler（5 例） | 4 FAIL / 1 PASS | #1686 半像元足迹、上采样 4×4→8×8 边缘 NoData、亚像元平移边缘丢失、越界判定 |
| test_r22_science_random（6 例） | 首版 1 FAIL | 黄金值自身的求值顺序缺陷（见下"发现"），非产品源码 |

汇总输出（节选）：
```
test cases: 7 | 3 passed | 4 failed (numerics)
test cases: 5 | 1 passed | 4 failed (resampler)
REQUIRE( d == Approx( 2.82842712474619029 ) ) with expansion: 2.449489832f
REQUIRE( stats.stddev == Approx( 0.25 ) ) with expansion: 64.0f
```

## GREEN（修复后第一跑，2026-10-26T01:0x）

| 测试 | 结果 |
|---|---|
| test_r22_science_numerics | All passed（351,467 断言 / 7 例；含 3200+ 随机 mask 与暴力 O(N²) 对拍零错误） |
| test_r22_science_resampler | All passed（53 / 5） |
| test_r22_science_random | All passed（201,069 / 6） |
| test_primitives5（基线族） | All passed（171 / 17） |
| test_math_utils（基线族） | All passed（77 / 6） |
| test_resampler（基线族，含按新足迹语义更新的越界例） | All passed（434 / 13） |
| test_feature_matcher（RANSAC 序列变化） | All passed（58 / 4） |
| test_endmember_extraction（PPI 序列变化） | All passed（72 / 4） |
| test_classifier_kmeans（reservoir 序列变化） | All passed（2202 / 4） |
| test_classification_pipeline | All passed（240 / 26） |
| test_obia_segmentation（multires + Kahan 累加） | All passed（850 / 14） |
| test_d14_geometric_registration_e2e（warp 边界） | All passed（8351 / 3） |
| test_rs_operators（rs:proximity） | All passed（9081 / 82） |

## GREEN×2（第二跑，2026-10-26T01:1x）
numerics/resampler/random/primitives5/math_utils/resampler/feature_matcher/
endmember_extraction/classifier_kmeans 九个二进制全部重复通过（同上数字）。

## 过程中发现并修正的缺陷（开发自身）
1. `uniformUnit53`/`nextU64` 初版把两个 `rng()` 调用放进同一表达式——C++
   求值顺序未指定，g++ 在不同上下文产生不同序列（正是 #1685 要消灭的
   缺陷类别）。已改为命名局部变量显式顺序，黄金值由独立 Python mt19937
   参考实现权威重算（mt19937 引擎本身为标准固定算法）。
2. 测试黄金值最初经 printf 多参数生成，同样受求值顺序影响，已弃用。

## 容差依据（非放宽掩盖）
- #1684 10000.3±0.5：ε=1%（朴素 double 累积噪声实测 <0.2%；能抓住 17% 级回归）。
- #1684 1e6 公式精确性：测试内以偏移平移精确重构 sum/sumSq（Sterbenz 精确
  相减），ε=1e-4 验证公式本身；调用方朴素累积的 ~√N·ε 下限已在
  math_utils.h 契约文档化，生产端 rs_segment_features.cpp 已加 Kahan。
- #1707 对拍：1e-4 margin（整数网格平方距离精确相等，缺陷误差为 2+ 平方单位）。
