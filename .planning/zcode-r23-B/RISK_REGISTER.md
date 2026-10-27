# R23-B RISK_REGISTER

| ID | 风险 | 概率/影响 | 缓解 | 状态 |
|---|---|---|---|---|
| R1 | 构建锁竞争：r23-A/C 并行占用 flock，本轨批量构建等待数小时 | 高/中 | 单次批量构建所有目标（单次持锁）；期间推进代码/文档工作；超时重试 | 进行中（已排队） |
| R2 | stub-compile gate 依赖系统 Qt/OpenCV 头路径，跨机漂移 | 低/低 | include 由 CMake configure 时从 Qt6::Core/OpenCV 目标属性注入，非硬编码 | 已缓解 |
| R3 | W2 门过紧拒绝合法清单（false positive 破坏既有模型） | 中/高 | 仅 heads[] 已声明时生效；logit+labels 放行；全 models/ 清单扫描无 role+format 冲突组合（segmentation/probability 主流） | 待 GREEN 全量清单跑后确认 |
| R4 | W3 新增 store API 事务语义与既有 saveMetricRecordsBatch/upsertRun 行为不一致 | 中/中 | 复用同一底层 exec 路径；新旧 API 行为等价性测试 | W3 实施时处理 |
| R5 | master 在本轨 PR 前前移（他轨先合并） | 中/中 | push 前 fetch + merge-base 比对；tests/CMakeLists.txt 逐字节复核兄弟注册块 | push 前执行 |
| R6 | #1491 线上 CI 失败无法本机归因（CI 无完成 run） | 中/低 | 本机同 GDAL 版本实跑 test_io_vector_contract；绿则记 UNVERIFIABLE(线上) 而非臆断 | 待构建 |
| R7 | r21-B A5 崩溃窗口修复若做跨表事务，导入器补偿语义删除后补偿失败路径行为变化 | 低/中 | 保留 warning 通道语义：事务失败本身即完整错误信息 | W3 决策点 |
| R8 | 上下文/时长限制导致 W3/W4 无法完成 | 中/低 | 每 slice 独立 commit；LOOP_LEDGER 记录续跑指针；未完成项如实标 PARTIAL/NOT_STARTED | 持续 |
| R9 | #1637 sub-4（probeRemote 重定向）为 latent 无生产调用方，修复价值低但改动面涉及共享 HTTP 传输 | 中/低 | 优先级最低；时间不足则仅记录（与 r22-data 遗留风险表一致） | 待定 |

## R1 轮追加（2026-10-27）
- R10（R1 P3-3，记录不修）：probeRemote 重写后重复响应头的平局语义从"后者胜"变为"先者胜"（HttpFetchResult::headers 契约）。仅当源站发出自相矛盾的重复 content-type 头时可观测（病态、RFC 未定义）；且新代码顺带修复了旧循环的真实顺序依赖缺陷（Content-Range 证明被后到的 Accept-Ranges: none 覆写）。改回反而重新引入非确定性。
- R1 P3-2 澄清（不改代码）：upsertRunWithMetric 公共包装在 experiment_store.cpp:958 持有 QReadLocker（与 upsertRun:932 完全对称，Impl 内 :982 再持）；评审原文自注 "safe as written"，经复核为误读。
- R1 P3-1/P3-4 已修（注释纠错 + 文档测试编号去配置化）。
