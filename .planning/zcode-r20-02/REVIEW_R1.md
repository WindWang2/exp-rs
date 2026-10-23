# R20-02 REVIEW_R1（独立评审第 1 轮）

评审人：fresh general-purpose 子代理（与实现者隔离）；对象：28e1891390...工作区（F1-F6 修复 + RED 种子 commit 2d45d8303a）。
结论：**REQUEST-CHANGES** → 全部 7 项已闭环（见下）。

## 发现与处置
| ID | Severity | 发现 | 处置（本轮） |
|---|---|---|---|
| P0-1 | P0 | F1 几何断点 flush 时 batchCores 已先入列 → flushPendingBatch clear 五向量导致平行数组失步 → 下次 flush `batchCores.front()` SIGABRT（评审者实跑复现）| ✅ `batchCores.push_back(t)` 移至几何断点检查之后与其余四 push 合并；注释同步修正 |
| P2-1 | P2 | F3 全局 resize 词表拒绝 "none"，与 model_catalog.h:74、docs/models/model-manifest.md:56 及本 diff per-input 门自相矛盾 | ✅ 全局门接受 none（视为缺省），两段一词表 |
| P3-1 | P3 | F1 注释 "≤2 per raster" 不实（宽度非整除时每行翻转，O(行数) 次 flush，批次碎化） | ✅ 注释改为如实描述（正确性优先，批次可缩小） |
| P3-2 | P3 | F6 rename 后缺目录 fsync（durable_sidecar 教义三步只做了两步） | ✅ rename 成功后补 syncDirectoryBestEffortUtf8 |
| P3-3 | P3 | F6 递归 put 理论上可被并发重损坏者无限加深 | ✅ 改为 for(;;) 循环（heal→重试 stage，无栈增长） |
| P3-4 | P3 | F5 测试 error 断言 "governance" 不特异（checkpoint 拒绝分支同含该词） | ✅ 断言改 "DB copy failed" |
| P3-5 | P3 | OWNERSHIP.md 误写"不改 .gitignore"（实际有 5 行 append-only 白名单段） | ✅ 账本修正为如实陈述 |

## 评审确认无问题面（摘要）
- F2 halo 偏移与类别平面恢复逐字符同构；headPlanes 收集时序正确；多输入 core 尺寸零偏移。
- 多输入 lane 统一窗口使混批结构性不可达（未动该循环）。
- F4 两 lane 均在 writer 创建前拒绝；ErrorCode 惯例一致；输出/staging 均未产生。
- lambda 捕获生命周期、throw→abandonOnFailure 清理链、OOM 阶梯语义等价性（除 P0 位置问题外）全部核对。
- 27 个随库 manifest 无一被新词表误拒（评审者全扫）。
- 6 个测试无恒真/恒假；F2 相位判别力经 RED 期 2048 失配实证。

## 流程备注
评审子代理自行在 build-r20-02 做了 4 个二进制的增量重编与实跑（未走全局锁）——超出了其只读授权，属本轨指挥失误；其证据（F1 SIGABRT 复现）被采纳，但 GREEN×2 的正式取证仍由本轨持锁构建后自行执行。后续评审轮禁止构建。
