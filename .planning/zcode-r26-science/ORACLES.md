# ORACLES — 独立参考（非复制生产实现）

| WP | Oracle 参考 | 断言 |
|----|------------|------|
| S1 | 纯算术：clip 100%/两侧=全范围，线性拉伸闭式 | pct∈{100,1000,-5} → [min,max] 拉伸；pct=2 回归钳 |
| S2 | 契约（头文件"cap kept samples"）+ 计数论证：128 有效候选 cap=10 → 10；8 有效 → 8 | classCounts + 无 NoData 行 + 二次运行一致 |
| S3 | PAV 池化语义：重复分数混合标签 → 单一 pooled knot (rate=0.5)；序列化契约 x 严格递增 | knot 严格递增 + toJson/fromJson 往返 + apply=0.6/0.4（行归一） |
| S4 | scipy kendalltau tau-b 参考值 0.81649658；Gilbert varS 公式含时间并列项 | tau/varS 数值 + 无并列回归 1.0 |
| S5 | 文档化算法的排序类参考模拟（portable Fisher-Yates + mt19937）+ 保序重命名不变性 | 折内样本序列逐一相等 |
| S6 | 簇号≠类号命名空间论证：remap 失败时精度必须 0 而非巧合 1 | foldAccuracies==0 |
| S7 | ADR 0060 规则文本"tie → smaller id"，key=0 是合法投票键 | {0:3,5:3}→0 |
| S8 | 分割器 8连通对象定义：对角相邻同标签 = 同一对象 | diagonal 光栅 polygonize 后要素数==1 |
| S9 | 姊妹内核 change_detection.cpp logRatio 语义（非有限→NaN，负值钳 0+eps） | NaN 传播 + log((2+eps)/eps) |
| S10 | US survey foot 精确定义 0.30480060960121924 m/ft；面积换算平方 | changedArea==4×ft²(m² 换算) |
| S11 | 契约：evaluate 接受特征矩阵，dtype 不应改变可用性 | CV_64F 输入 ok() 且折精度正确 |
| S12 | fail-closed 纪律（与 fit 的 NaN 拒绝同构） | method=7/version=2 → false |
