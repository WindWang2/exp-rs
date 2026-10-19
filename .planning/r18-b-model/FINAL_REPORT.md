# R18-B Final Report — 遥感 AI 模型推理：算子运行、GPU/CPU 生命周期与分块恢复

- **Branch:** `agent/r18-b-model-runtime-perf`（worktree `/home/kevin/project/exp-rs-r18-b-model-runtime`）
- **Base:** `origin/master` = `2d2881631fc821537100c00da677888e3a31ea8f`（PR #1533 merge）
- **Commits:** 7 — a6ac18bcd (restore) · e6e91f71e (fixes) · 59aa6e420 (tests+evidence) · 498012d25 (R1 hardening) · e0d334384 (R1 report) · 0592b1913 (R2 fixes+report, includes tracked references)
- **Diff scope:** src/operators/runtime (4 files) + src/runtime/chunk (2) + src/operators/framework/{model_catalog,chunked_run} (restoration) + tests (6 modified, 6 restored-new, 3 fixtures) + .planning/r18-b-model/** + .gitignore + tests/CMakeLists.txt (r15-C block re-anchor)

## 1. 关键事故发现（本轨最大交付）

**合并 a8d5cd510（#1516，r15-D "keep both track ignore rules"）把已合并的 r15-C 轨道（#1515）整体静默回滚**（−3893 行：14 个源码修复、6 个新测试、4 个被修改测试、规划文档）。git 证据链：`f326d8ca9`（#1515 merge）持有 → `a8d5cd510` 全缺 → 之后零提交触碰。本轨第一 commit 从 `f326d8ca9` 逐字恢复（28/29 路径 diff 为空；CMake 注册块按原锚点重插 + 2 行说明注释）。恢复后 r15-C 全套 26 用例/179 断言对恢复源码全绿 = 恢复自洽。

跨轨：同一合并还回滚了 `src/processing/algorithms/image_fusion.cpp` 的 r15-a 修复（R18-A 域）与 `.planning/r15-a` 文档 → CROSS_TRACK_FINDINGS.md，未越界修复。

## 2. 新缺陷修复（全部 RED→fix→GREEN×2，除注明的两个契约固定）

| WP | 缺陷 | 级别 | 证据 |
|---|---|---|---|
| W4b | FeatherAccumulator::flushThrough 清理已 finalize 行但不搬移 pending 行，而 offset 是线性映射（y−m_top）——**每个高于一个窗口的 feather 栅格都把权重/标记混入错误的行**（自 Platform 9.0 起；M5 回归栅格恰好单窗口从未滑动） | **P0** | W4 测试 RED 200 断言 + 插桩追踪（exc=12 时 offset 12-15 被重解读为行 24-27） |
| W3 | runMultiInput 的 EO 强制校准 preflight 在 publish/disarm **之后**——拒绝的运行留下完整已发布产品 | **P1** | RED 3 断言（产品+sidecar+park 残留） |
| W4 | 高于窗口的 all-NoData 延迟块：标记被窗口边界丢弃 → 有效 tile 的 halo 把预测**伪造到 NoData 地物**；尾部越窗使 finish() 读越界行 | **P1** | 同 W4 测试（头/尾两形态） |
| W5 | scene 波长窗 preflight 不传有效波段——role i 永远对照文件 band i+1（错拒/错放） | P2 | RED：bands={2} 被错拒（正是预测方向） |
| W6 | model-publish lane 零 fsync——掉电后 rename 完好但内容可截断（检测 lane #1174 早已 fsync） | P2 | 与 #1174 代码一致性 + 发布套件在 fsync 路径回归 |
| W7 | ChunkGraph sink 逃避单消费者守卫（两种注册序都静默分片 tiles）；graph/pipeline 生产者契约门放行全默认 payload（0==0） | P2 | RED：守卫未抛 + null-payload 使 graph 挂死 |
| W1 | OpenCV infer() 在锁外 clone forward() 内部 blob（OpenCV 4.x 语义下共享会话撕裂；inferMulti 早已锁内 clone） | P1* | 契约固定（宿主 OpenCV 5.0 每次 forward 分配新缓冲——探针证据；如实不声称 RED） |
| W2 | python worker 写超时分支不 fail-close（半写请求的迟到答案会喂给下一请求） | P1* | 防御性对齐（分支当前不可达：读期限先行且已 fail-close——推理链记录在案） |

R1 附加硬化：flushThrough 终止行钳制到窗口底（读侧结构性防越界）；R2 附加：add() 写侧 CV_Assert、审计尾档入库、span 文档实改。

## 3. 验证

- **RED**：W3(3)/W4(200)/W5(1)/W7(no-throw + 挂死) 真实失败于恢复态代码。
- **GREEN ×2**（两轮完全一致）：13 个套件全绿——model_runtime_9 1226、multimodal 8332、eo_platform_10 581、chunk_graph 371、6×r15_C 179、model_ensemble 120、r14_B 18、provider_python#r18B 5；model_tasks 12815/12816（1 断言环境受限：宿主无 proj.db → GDAL importFromEPSG 静默失败 → CRS 空；断言早于 r15-C、与本轨 diff 无关）。
- **性能**（Oracle-C）：bench ×3（1223/1027/1046 ms 墙钟；冷载 400ms、721 tiles/s、取消 19.6ms）；修复开销有界分析在 PERFORMANCE.md；**不声称任何加速**。
- **B10**：CLI `--operator-help` 四算子全 rc=0；深路径由 runModelInference 套件覆盖。
- **NOT_EXECUTED**：CUDA/TRT/OpenVINO/ORT 真机（无硬件——CUDA bench 门控信息为证）、掉电注入、Windows 路径、test_device_planner（宿主挂起，r15-C 已记录）、线上 CI（#1491：master CI 自 10-11 无完成记录；按任务书不等）。

## 4. 独立 Review（两轮，均为未参与编写的新鲜上下文）

- **R1**：PASS-WITH-NITS（8×P3）——恢复逐字性、7 项修复对抗性追踪全过；建议的结构性钳制已采纳（498012d25）。
- **R2**：PASS-WITH-NITS（3×P3 + 1 note）——R1 修复证明在一切可达调用模式下无丢失写；恢复零死引用；跨轨调用者异常预期不变；抓到 R1 幻影"已修"（span 文档未真正改上）→ 已真正修复并入库审计尾档。

## 5. 遗留（KNOWN_FINDINGS.md，16+3 项已验证未修的 P2/P3）

代表性：ORT 无超时车道、注册表锁内会话析构（python kill 阻塞 acquire ≤5s）、旋转地理变换检测框、ensemble ±Inf 门、DetectionPublishGuard Window-B 缺失、检测 lane 无 EO preflight、Windows journal 清理、ensemble 成员加载不响应取消、chunk sink 进度首瓦片 1.0 等。

## 6. 维护者建议

对每个触碰 `.gitignore` 的合并运行 `git diff --stat <merge>^1 <merge>` 并要求删除可解释——#1516 的"keep both ignore rules"冲突解决正是本次全轨回滚的根因模式。
