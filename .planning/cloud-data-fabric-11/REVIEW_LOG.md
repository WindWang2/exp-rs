# REVIEW_LOG — cloud-data-fabric-11

## Round 1 — 主 agent 自审（实现过程中）

- mirror-first 与 noDataSet 兜底覆写冲突 → 共享 ensureNoData lambda。
- chunk 键基升级（indexKey）造成 10.0 在线路径命中回归 → readWindow 双键查找。
- 镜像命中分支悬垂 BandInfo 指针 → reader 提升作用域。
- prefetch 双开（mirror 声明时每 chunk 2 次 open）→ 单开 + 预算前置。

## Round 2 — 独立对抗 review（subagent #2，全 diff origin/master...HEAD，5 commits）

Reviewer 轴：科学正确性 / 并发取消生命周期 / 原子性失败 / API 兼容 / 安全 / 测试可信度 /
文档声明漂移。**P0=1、P1=4、P2=5、P3=8，全部有 file:line 证据。**

| # | sev | finding | disposition |
|---|---|---|---|
| 1 | P0 | multidim 空间切片偏移计算后被丢弃 → 执行从原点读错误窗口 | fixed：mMultidimSelection["y"/"x"] 记录源偏移（chunk_plan.cpp），执行经 sourceIndexOf 映射；新增 geotransform 描述符的 known-answer 测试 |
| 2 | P1 | fetchRangeVsi Seek 以 32 位 long 截断 ≥2GiB 偏移 | fixed：直传 64 位 vsi_l_offset |
| 3 | P1 | mirror-first scatter 用 chunk-local 窗口当资产像素坐标 → 非原点 chunk 静默不贡献（10.0 post-open 同病） | fixed：scatter 边界窗改为资产像素窗（偏移保留、尺寸 clamp）；Oracle 1 扩非原点 chunk byte-equal 断言 |
| 4 | P1 | 无时间轴 multidim 存储计划 0 chunk；时间切片被静默忽略 | fixed：无时间轴不加 phantom time 维；时间切片 typed Unsupported；新增 [band,y,x] 测试 |
| 5 | P1 | mirror-first 绕过 D-1005 CRS 守卫（离线/在线分叉） | fixed：CRS 检查提升至 mirror 分支前 |
| 6 | P2 | sha256 记录但从不校验；CHANGELOG 声明超前 | fixed：resolveMirrorArtifact 强制校验 sha256（mismatch = typed miss）；新增同尺寸篡改用例 |
| 7 | P2 | 异常路径跳过终写（≤31 条不可见） | fixed：FinalFlushGuard RAII |
| 8 | P2 | 时间切片静默丢弃不可解析 instants | fixed：typed refusal |
| 9 | P2 | gate 复选框/回归表未填；损坏注入依赖目录序；Oracle 1 只证原点 | fixed：表与 gate 填写；损坏目标确定性推导；非原点 oracle |
| 10 | P2 | 镜像 chunk 丢源 band NoData（离线/在线 FirstWins 分叉） | fixed：materializer 写入源 band NoData/dtype/description |
| 11 | P3 | canonicalObjectKey VSI 分支不拒 ?/@/# | fixed |
| 12 | P3 | invalidateResource 注释与实现不符 | fixed：context 非空时同时失效共享键 |
| 13 | P3 | executeMultidimChunks 计数/预算中断 ok 语义 | fixed：skippedBudget ⇒ ok=false；计数语义注释 |
| 14 | P3 | CLI --max-bytes 未验 parse | fixed |
| 15 | P3 | mirrorChunks(FabricPlan) 重复探测 | accepted（效率；身份状态合一为 follow-up） |
| 16 | P3 | GCS generation 字段化过度声明 | fixed：文档改为"通道就绪、字段化 follow-up" |
| 17 | P3 | Windows RSS gate 放宽未记 matrix | fixed：TEST_MATRIX 补记 |
| 18 | P3 | catalog canonical 化或孤立 10.0 Windows v1 镜像键 | accepted：PR_BODY 兼容注记 |

**结论：修复后 P0=0、P1=0。** 全量 fabric 家族 11 套件 × 2 连续全绿（TEST_MATRIX 最终 gate）。
