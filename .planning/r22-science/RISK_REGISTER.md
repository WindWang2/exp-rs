# R22-01 science 风险登记

| 风险 | 等级 | 缓解 |
|---|---|---|
| git fetch https 超时 | 中 | 已用 gh api 验证远端 SHA 与本地一致（911e0b17）；push 前重试 fetch |
| #1676 合并顺序导致 math_utils/kmeans 文本冲突 | 中 | 改动不同函数区域；PR body 说明协调策略；push 前 diff 复查 |
| #1686 修复改变既有 test_resampler 越界断言语义 | 高（须透明） | 该断言直接编码缺陷契约（-0.25 期望 NoData）；按足迹语义更新为 -0.75，PR body 明示，非删除测试 |
| #1685 改变 RNG 序列导致依赖旧行为的测试失败 | 中 | 逐一运行受影响测试族（split/kmeans/endmember/registration/obia）；任何断言精确随机结果的测试按新黄金值更新并说明 |
| 共享构建机资源竞争 | 中 | 全部构建走 /tmp/exp-rs-r21-build.lock，-j4 上限，记录 free -h |
| GDAL/Qt vendor 链接伪失败 | 中 | 复用 R21-A 的 LD_LIBRARY_PATH/PROJ_LIB/shim 环境；环境性失败记 KNOWN_BASELINE_FAIL |
| distance_transform 修复引入性能回退（额外缓冲） | 低 | 缓冲由调用方一次分配（row/col 各一），O(n) 常数级；A5 性能门下复核 |
| stats 修复改变 float 输出位模式 | 低 | mean 语义不变（仍 float 输出）；stddev 数值更准；既有测试用 Approx 容差 |
