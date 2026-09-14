# REVIEW_LOG — cloud-data-fabric-datacube-10

Phase 7 起填写。格式（逐条）：

```
R-<n> | <subagent|inline> | P0..P3 | <一句话> | 位置 文件:行 | 处置
（fixed / accepted-debt / wontfix + 理由） | 修复证据（命令/测试名）
```

## Round 0（主 agent 自查 + Phase 6 裁决）

R-000 | inline | P1 | io:reproject srcCrsOverride 死参数（#957 遗留 finding，
Phase 0 起登记候选） | src/operators/io/io_operators.cpp:306（校验读而不传） +
raster_convert.cpp WarpOptions 无源 CRS 字段 | **fixed**：WarpOptions.sourceCrsOverride
+ warp 写 -s_srs + reproject 传参 + test_io_operators F-OPS-4 回归（全绿，95 断言） |
D-1013 执行记录。

## Subagent #1 — 架构 + 科学/语义正确性（A-*，18 findings）

A-R1 | P1 | 协商网格以 explicitGrid=false 存入 plan，执行期单资产重建重协商出不同网格
（错区读取或中止） | query_planner.cpp:315,552-556,680 | **fixed**：协商后
`plan.mGrid.explicitGrid=true`（网格是计划的决定）| 顺带关闭 A-R13（probeLimit 漂移）。
A-R2 | P2 | AWS 匿名键装了两次同一现代拼写，legacy 拼写从未安装 | object_store.cpp:255-256 |
**fixed**：modern+legacy 两拼写；恢复仍精确幂等。
A-R3 | P2 | io:cube_window 输出未声明 NoData 且 Float64→Float32 降精度 |
io_fabric_operators.cpp:263-267 | **fixed**：band 声明 NoData + dtype=Float64。
A-R4 | P2 | cube 选择序用 UTC 字符串比较（同秒不同小数形状反序） | virtual_cube.cpp:89-95 |
**fixed**：epoch nanos 比较。
A-R5 | P2 | 远程资产离线镜像重放不可达（镜像查询前先网络身份探针） | virtual_cube.cpp:526-554 |
**fixed(部分)+accepted-debt**：探针失败不再中止读取；远程离线重放需无网络 token 机制，
记 follow-up（本地文件镜像离线重放已验证）。
A-R6 | P2 | 派生网格无 authid 时 io:cube_window 读完即抛 | io_fabric_operators.cpp:269 |
**fixed**：authid 空则发布无 CRS 标签输出。
A-R7 | P2 | mirror 内联窗口映射非符号安全，违反"ONE mapping rule" | mirror.cpp:290-306 |
**fixed**：改调 virtualCubeSourceWindow。
A-R8 | P2 | executeChunks O(total chunks) 累积 outcome，违反 D-1008 | query_planner.cpp |
**fixed**：保留窗口 ≤1024 + outcomesDropped 计数。
A-R9/B-R6 | P3/P2 | planner 分页无 backstop/maxItems 停止 | query_planner.cpp:235-264 |
**fixed**：统一 walkPages。
A-R10 | P3 | mirror clamp 后 provenance 报告 clamp 前窗口 | virtual_cube.cpp:522 | **fixed**。
A-R11 | P3 | prefetch mirror 探针在 try 外，单资产失败中止 walk | prefetch.cpp:126 | **fixed**。
A-R12 | P3 | 本地遍历截断从不外显 | catalog_service.cpp:832 | **fixed**：达上限页诚实上报。
A-R13 | P3 | gridProbeLimit 漂移 + mirror"no probe"注释为假 | 多处 | **fixed**（同 A-R1）。
A-R14 | P3 | 字节估算上界被注释为求和 | query_planner.cpp:377-388 | **fixed**：注释改为
deliberate upper bound。
A-R15 | P3 | 镜像 chunk 无地理配准（死像元网格） | mirror.cpp:342-343 | **fixed**：真实源范围
geotransform。
A-R16 | P3 | MirrorReport::toJson 声明未定义 | mirror.h:87 | **fixed**：完整实现。
A-R17 | P3 | mTimeOffset/mBandOffset 死代码 + 头文档与切片行为矛盾 | chunk_plan.* | **fixed**。
A-R18 | P3 | 重复 include + prefetch 网格守卫条件错误 | io_fabric_operators.cpp; prefetch.cpp:63 |
**fixed**。

## Subagent #2 — 性能/并发/生命周期/测试可信度（B-*，18 findings）

B-R1 | P1 | **planFabric 在匹配数 > maxItems（默认 1000）的本地树上死循环**（0 记录页 +
hasMore 原偏移，无退出） | query_planner.cpp:253-264 | **fixed**：walkPages（计数停止 +
无进展退出 + 100000 页 guard）。
B-R2 | P1 | prefetch maxBytes=0 文档"有界默认"实为无界 | prefetch.h vs prefetch.cpp |
**fixed**：默认 = 计划字节估算；估算 0（无字节事实）→ 预算不启用（文档明示）。
B-R3 | P1 | mirror maxBytes=0 同病（u64 max） | mirror.h:55-57 | **fixed**：预算按声明逻辑
字节记账，报告如实给文件字节。
B-R4 | 同 A-R8 | **fixed**。
B-R5 | P2 | PrefetchReport.chunks 无界累积 | prefetch.cpp | **fixed**：≤1024 + 计数。
B-R6 | 同 A-R9 | **fixed**。
B-R7 | P2 | mirror 清单 I/O O(chunks²) | mirror.cpp:313,351-369 | **fixed**：内存清单命中 +
每 32 chunk 节流重写 + 结束必写。
B-R8 | P2 | resolveMirrorHit 坏类型清单项抛 Json::LogicError 逃出 GeoError catch |
mirror.cpp:132,164 | **fixed**：类型检查按 corrupt 跳过。
B-R9 | P2 | fabricIntentFromJson 坏类型 JSON 抛 Json 异常穿透算子边界 | query_planner.cpp |
**fixed**：catch Json::Exception → GeoError(InvalidArgument)。
B-R10 | P2 | 预算机制从未对真实远端字节测试 | test_io_fabric_plan.cpp | **fixed**：新增
http loopback 真字节预算测试（warm→hit→starved）。该测试立即暴露 M-R1 键名缺陷。
B-R11 | P3 | skippedCancel 恒 0 | 各 walk | **fixed**：取消剩余计入。
B-R12 | P3 | findObjectStoreProfile 返回裸指针（注册重分配竞争） | object_store.cpp:103-112 |
**fixed**：按值返回。
B-R13 | P3 | 析构 VSICurlClearCache 在 D-1003 窗口外 | object_store.cpp:328 | **fixed**：契约
文档扩展覆盖析构。
B-R14 | P3 | TopKSelector offer 按值拷贝每个落选记录 | query_planner.cpp:84 | **accepted**：
churn 已被 RSS 断言覆盖为 O(1)MiB；避免选择器所有权复杂化。
B-R15 | P3 | HttpS3Server 析构先 join 后 shutdown + send 无超时 | http_s3_server.h:139-148 |
**fixed**：join 前 shutdown（照 range server 教程）；send 超时 accepted（recv 有界假设）。
B-R16 | P3 | MirrorOptions.blockSize 死字段+注释矛盾 | mirror.h:60 | **fixed**：注释更正为
实现现实（接线为后续）。
B-R17 | P3 | mirror/prefetch 每 chunk 线性扫资产+双开 | prefetch.cpp; mirror.cpp | **fixed**：
byId 映射 + 探针 try 化。
B-R18 | P3 | 字节预算依赖进程独占未文档化 | prefetch.cpp:170-178 | **fixed**：maxBytes 注释
明示。

## 主 agent 复核补充（review 触发的再验证）

M-R1 | P1 | **prefetch telemetry 键名不匹配**（bytesFetched vs bytes_fetched）→ 预算与
cache-hit 分类恒失效；B-R10 新测试直接暴露 | prefetch.cpp:170-178 | **fixed**：键名修正 +
真字节断言全绿。
M-R2 | P1 | **fabricCachedPath 对 /vsis3/ 产出坏的缓存组合路径**（fallback 直读且误报
cache-hit） | object_store.cpp | **fixed**：缓存包装限定 http(s)；S3 块缓存键记 follow-up
（docs/io/fabric-10.md 边界）。
M-R3 | P1 | planFabric move 后读 moved-from 成员（网格/查询静默丢失） | query_planner.cpp |
**fixed**（Phase 5；本轮回扫确认全覆盖）。
M-R4 | P2 | records 后端在 planner 内整目录拷贝（违反 D-1009） | query_planner.cpp |
**fixed**（Phase 5：by-value + move）。

## 汇总

* P0：0。P1：5 项（A-R1、B-R1、B-R2、B-R3、M-R1/M-R2/M-R3 并组）——**全部 fixed**。
* P2：13 —— 12 fixed，1 accepted-debt（A-R5 远程离线镜像重放 → follow-up）。
* P3：16 —— 13 fixed，3 accepted（B-R14、B-R15、B-R16）。
* 终态：fabric 六套件 344 断言全绿 + io 家族回归 7 套件全绿。
