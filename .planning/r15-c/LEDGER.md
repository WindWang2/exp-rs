# R15-C Ledger — 会话进度与决策记录

## 宿主事实

- 宿主：Windows（DSH/DeepSeek Harness），非 Google Antigravity；`/teamwork-preview` 与
  `/goal-loop` 未在本宿主注册（沿用 R14-B DEGRADATION 记录）。等价结构：主编排 →
  实现（本 Agent）→ **独立只读审计 subagents**（不写生产代码）→ 独立 review subagent
  （修复完成后介入，同样不写本轨代码）。
- 工具链：MSVC 2022 + Ninja + Qt 6.8 + vcpkg 共享依赖树。本轨构建树
  `build-r15-c`（独立 CMakeCache）。全局构建互斥锁（命名 Mutex）落实在
  `.ps1_build_r15c.ps1`。

## 时间线

- **R0 事实冻结**：origin/master = `46791bc65c6f552ebbcf723bb91892736d56f319`
  （#1509 合并日）。open PR 仅 #1514/#1513；#1505/#1512 已合并（快照锁定解除）。
  写入 `.planning/r15-c/{BASELINE,OWNERSHIP,ISSUE_PR_REVIEW_MATRIX,ORACLES}.md`。
- **R0 worktree**：`exp-rs-r15-c-20261016` @ base，分支
  `hardening/r15-C-model-runtime`。主 checkout 未触碰。
- **R0 configure**：`build-r15-c` 配置发现共享 vcpkg_installed 树对当前 manifest
  不完整（缺 ~70 包，含 jsoncpp/opencv），configure 正在补装（二进制缓存命中部分）。
  该树为依赖缓存（非他轨构建树），补装对全主机有益。
- **R1 并行只读审计**（6 个 subagent，全部只读，未写任何文件）：
  C03/C09、C07/C08、C10/C13、C12/C11/C04、C14/C16/C05。C03 首个 agent 两次失败
  （基础设施），改由更小范围重派；其余 5 个返回完整报告（发现均已交叉核验到
  文件:行号）。
- **R2 修复实现**（本 Agent，全部位于本轨所有权内）：
  1. C02 注册期交叉约束（model_catalog.cpp）：detection 固定帧契约、square-head
     显式定义规则、heads[].dtype 词表。
  2. C13 F1/F2 publish guard 后缀完备收养（model_publish.cpp）。
  3. C16 B2 provider 不可用原因注入通用错误（model_runtime.cpp/.h）。
  4. C11 F1-chunk ReduceConcurrency 梯级被采纳（chunked_run.cpp）。
  5. C11 F2/F3/F4 VramLedger 溢出/幻影 holder/部分释放（device_planner.cpp）。
  6. C14 B5 conf/nms_iou 覆盖范围校验（model_execution_service.cpp）。
  7. C07 单输入 min_valid_coverage 门（tile_inference_engine.cpp）。
  8. C05 B3 多输入能力预检 + python worker 协商 seams 一致（tile_inference_engine.cpp,
      python_worker_provider.cpp）。
  9. C05 B4 跨 provider 失败分类归一（model_runtime.cpp）。
  10. C09 vote agreement NoData 哨兵（model_ensemble.cpp）。
  11. C04 F5/F6 env 覆盖设备数封顶 + CSV 补齐（model_runtime.cpp）。
  12. C12 F7 join 进度分母、F9 pipeline 一次性运行守卫（chunk_graph.*, chunk_pipeline.*）。
  13. C12 F8 / C11 F12 文档化范围声明（memory_planner.h, tile_inference_engine.cpp）。
  14. C11 F11 VRAM 预算拒绝映射 ResourceBudgetExceeded（model_execution_service.cpp）。
  15. C05 B7/B8/B9 OpenVINO/TensorRT deviceName 词表、artifactPath 真实路径、
      health/memoryEstimate（编译期不可见车道，代码级修复）。
  16. C08 dedup 键 lround→llround（detection_postprocess.cpp）。
  17. C16 B10 能力门测试反转为正向断言（test_eo_platform_10.cpp）。
- **R2 测试**：新增 5 个 `tests/test_r15_C_*.cpp`（C02/F1/F2/F1-chunk+F2-F4 ledger/
  B2+B4+B3/C07），另在 test_model_tasks.cpp（B5）与 test_model_ensemble.cpp（C09）
  追加用例；tests/CMakeLists.txt 错开 #1513 尾部署名块纯追加注册。

## 决策

1. **C02 square-head 规则的唯一可计算形式**：head 通道数 C（由 classes 推出）==
   声明输入帧 W*H（dense head 的候选数）时拒绝 `auto`，要求显式
   channels_first/channels_last。典型 YOLO（C=6 vs 409600）不受影响；runtime 的
   square 拒绝保留为纵深防御。
2. **B1（HTTP provider 无 dispatcher 线程）不在本轨修复**：R14-B 已把永久挂钟改为
   typed refusal；实现 helper-thread exchange 是更大的生命周期改动，且
   model_execution_service 对所有入口统一走 JobEngine std::thread。记录为
   已知限制 + 候选后续（见 FINAL_REPORT 未决项）。
3. **B6（commonEstimate 漂移）不修**：位于 `src/operators/rs/**`，非本轨可写范围；
   已记录给对应 owner（同 B11 env-doctor）。
4. **F3-chunk（fence 晚于 stage）不修**：需要把 fence 获取前移到 tile 循环之前或
   stage 名 pid 化，触及 4 条 lane 的发布骨架；风险/收益比在剩余预算下不划算。
   记录为已知限制（并发同路径运行在 Windows 上响亮失败而非撕裂）。
5. **F10（eviction 在 registry 锁内销毁 session）不修**：纯延迟问题，改动 teardown
   顺序风险高。记录。

## 阻塞/风险

- 构建依赖 vcpkg 补装共享依赖树（进行中）；本轨所有 RED/GREEN 证据待其完成后取得。
- OpenVINO/TensorRT 车道本宿主无工具链：相关修复为代码级（compile-only），
  标注 NOT VERIFIED，不伪造 GREEN。

## Review 循环

- **R3 独立 Review 第 1 轮**（未写生产代码的 subagent）：BLOCK — 1×P1
  （VRAM→ResourceBudgetExceeded 投影无效：字符串匹配永远不匹配 + 无测试）、
  1×P2（DetectionPublishGuard fold 在 partial-disarm 窗口可撕裂新产品）、4×P3。
  处置：全部修复（P1 含 RED 验证；P2 改 removeBackupFamily 删除顺序为 park
  阶梯的镜像 + 回归用例；P3 三条），复验 GREEN×1（24 目标全过）。
  详见 REVIEW_ROUND1.md。
- **R4 独立 Review 第 2 轮**：**PASS**（无开放 P0/P1/P2），3×P3：
  (1) model_publish.cpp fold 注释仍描述旧删除顺序（已修，并加"勿改回"警告）；
  (2) "VRAM but" 投影两处拼写缺口（resolveDevice 混合条件消息 + fallback 链
  复合 reason）——已作为已知限制记入代码注释，类型化 reason 留待后续；
  (3) **记录更正**：提交 30e8af67e 称 "must be a 4-d" pattern "matched no
  message in the tree" 不准确——classifyInferenceError 先小写化，这两个
  pattern 对树中 "must be a 4-D … NCHW blob" 系列本是 LIVE 的；替换为
  ("must be" && "nchw") 后经 reviewer 对全部 8 条 nchw 消息逐个推演，
  分类与旧 pattern 完全一致（6 条 fed-input→ShapeMismatch；2 条 rank-4
  输出→Unknown），代码变更行为保持、健全，仅记录理由错误（此条即为更正）。
  详见 REVIEW_ROUND2.md。

## 提交序列

1. `c1c67b978` test(r15-c): C02 manifest registration cross-constraints
2. `c2a077ba1` harden(model-catalog): registration-time cross-constraints (C02)
3. `abd33bbff` harden(model-publish): suffix-complete crash adoption (C13)
4. `46169d6e4` harden(runtime): provider degradation/taxonomy/capability preflight (C05/C16)
5. `5440971d4` harden(chunk,ledger): admission ladder + VRAM ledger (C11/C12)
6. `d9ebe2427` harden(engine,service): coverage gate, vote sentinel, overrides, dedup (C07/C09/C14/C08)
7. `91f6299a4` docs(r15-c): baseline/ownership/matrix/oracles/ledger/performance
8. `30e8af67e` harden(r15-c): review round-1 fixes (VRAM projection, disarm-window tear, nits)
