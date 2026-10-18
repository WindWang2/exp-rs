# R16-A-T02 PERFORMANCE — WP-E 实测(结论:无真问题,不优化)

方法:g++ -O2 直连 build-dev/src/grader/libsicnu_grader.a 的独立探针
(/tmp/t02probe/perf_grader.cpp,未提交),8 criteria(4 stage + 4 metric,
rubric 总分 400)× N 条证据(每个 item 带 2 个 facts 成员),grade() 重复 5/3 次
取最好/最坏墙钟;内存用 getrusage(RUSAGE_SELF).ru_maxrss(本机无 /usr/bin/time -v,
以 getrusage 等价替代)。结果均为真实运行输出,无外推。

| 规模 | grade() 墙钟(最好/最坏) | 峰值 RSS | 摘要稳定 |
|---|---|---|---|
| 1e4 条(rubric 默认 hard cap) | 380.85 / 396.50 ms | 37.6 MiB | 5/5 字节一致 |
| 1e4 条,facts 置空(归因用) | 288.27 / 295.95 ms | 28.0 MiB | 5/5 |
| 1e5 条(预算抬到上限后可达) | 3963.70 / 4116.73 ms | 328.0 MiB | 3/3 |

归因与判定:

- 匹配引擎本身无 O(n×m):EvidenceIndex 按 key 建桶,判定是 8 criteria × 桶内
  线性扫描(≈8×1e4 次比较,微秒级)。墙钟主要花在 evidence.toJson() 树构建 +
  两条 SHA-256 摘要(canonicalizeJson 逐字节序列化)— 这是摘要契约的固有成本,
  facts 占比约 25%(380−288ms)。
- hard cap(1e4)下 <0.4s、38MiB:判定为可接受,无优化动作("真问题才优化")。
- WP-D 的深度预算在每个节点只增加一次 int 比较,对以上数字无可测影响
  (探针在修复后编译运行,数字即为修复后表现)。

faultlab 侧无性能工作:教学规模 fixture(16×16)整场景 run+digest 稳定在
ctest 单测 0.01–0.05s(见 TEST_MATRIX 的 fault lab 行耗时)。
