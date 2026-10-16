# R15-C Final Report — 既有遥感 AI 模型推理、分块执行与 GPU/CPU 容错

## 交付概要

- **Branch:** `hardening/r15-C-model-runtime`（worktree
  `C:\Users\wangj.KEVIN\projects\exp-rs-r15-c-20261016`）
- **BASE_SHA (origin/master):** `46791bc65c6f552ebbcf723bb91892736d56f319`
  （2026-10-16，#1509 合并日）
- **HEAD_SHA:** `44df74a27`（9 commits，ahead 9 / behind 0——建 PR 时复测）
- **PR:** 见文末链接（建 PR 后回填）
- **宿主事实（如实记录）:** 本宿主非 Google Antigravity；`/teamwork-preview` 与
  `/goal-loop` 未注册（与 R14-B 相同的 DEGRADATION 记录）。等价结构：主编排 →
  实现（本 Agent）→ **7 个只读审计 subagent**（C01–C17 覆盖）+ **2 轮独立
  review subagent**（均未编写本轨任何生产代码/测试，只读）。

## 修复索引（全部位于本轨所有权内；P0 数据损坏/崩溃未发现）

| WP | 缺陷（严重度） | 提交 |
|---|---|---|
| C13 | **P1** DetectionPublishGuard companion-park 崩溃后缀被删除 → 无 .shx 的不可打开矢量仍通过 provenance 校验 | abd33bbff |
| C05/C16 | **P1** 四个 provider 的可恢复文案是死代码；用户只拿到通用错误 | 46169d6e4 |
| C11/C12 | **P1** ReduceConcurrency 接纳计划被当作已接纳 → pipeline 超预算运行 | 5440971d4 |
| C13 | P2 ProductPublishGuard sidecar-park 后缀被销毁 → MissingSidecar 降级 | abd33bbff |
| C11 | P2 VramLedger int 溢出（UB）绕过接纳权威；幻影 holder；release 忽略数量 | 5440971d4 |
| C02 | P2 注册期交叉约束缺口（R14-B 显式遗留）：detection 固定帧契约、square-head 显式定义、heads dtype 词表 | c2a077ba1 |
| C14 | P2 conf/nms_iou 覆盖无范围校验 → 静默空产品/误导报错 | d9ebe2427 |
| C07 | P2 单输入引擎静默忽略 min_valid_coverage | d9ebe2427 |
| C05 | P2 多输入能力协商缺失（IO 之后才炸）；python worker 协商值被丢弃 | 46169d6e4 |
| C05 | P2 同一 dtype/shape 错误跨 provider 三种 ErrorCode | 46169d6e4 |
| C09 | P2 vote agreement NoData 量化成越界值（死 isnan 分支） | d9ebe2427 |
| C04 | P3 env 设备数无界（OOM kill）；VRAM CSV 静默丢数据 | 46169d6e4 |
| C05 | P2/P3 OV deviceName 词表、TRT/OV artifactPath 编造身份、health/memoryEstimate 悲观默认 | 46169d6e4 |
| C12 | P3 join 进度永远到不了 1.0；pipeline 双重运行重放旧错误 | 5440971d4 |
| C11 | P3 VRAM 预算拒绝映射 InvalidInputData | 30e8af67e（含 RED 验证） |
| C08 | P3 dedup 键 int 回绕（UB） | d9ebe2427 |
| C16 | P3 能力门测试在完整部署主机上零断言假通过 | 46169d6e4 |
| C13 | P2（review R1 发现）disarm 窗口折叠撕裂新产品 | 30e8af67e |

## Issue 分类（详见 ISSUE_PR_REVIEW_MATRIX.md）

- 本轨候选并已处理：#1455 残余经复核（corpse-session ledger 已 FIXED_ON_MASTER，
  回归复跑绿）；#1460 模型参数部分（C14：权威路径 CLEAN，参数漂移有机械强制；
  B5 覆盖范围已修；commonEstimate 漂移属 `src/operators/rs/**` 他轨，已记录）。
- FIXED_ON_MASTER（未重做）：#1447/#1488（#1509）、#1485/#1486/#1499（#1505 及
  R14-B 已验证）、#1459（#1501）。
- OUT_OF_SCOPE：#1490–#1495（CI/build）、#1489（i18n）、#1458、#1462–#1468 等
  他轨 cluster。

## 证据

- **构建**：MSVC 2022 Debug + Ninja `-j2`（全局命名互斥锁）；configure exit 0；
  25 个测试目标 link exit 0；`git diff --check` 干净。
- **RED**：stash 全部 src 修复后，8/9 新目标按预期失败（manifest 3/6、
  publish 2/3、chunk 4/5、detection 1/5、provider 3/3、contracts 1/2、
  model_tasks 1/20）；P1 修复另做单独 RED（exit 42）。C09 在 master 上通过
  （GDAL Float32→Byte 钳制掩盖，如实记录）。
- **GREEN ×3**：24 个目标全过（含修复后两次完整重跑 + review 修复后一次）：
  test_model_runtime 100394 assertions、test_model_failure_matrix 2158、
  test_r14_B_tile_engine 20695、test_multimodal_inference 8324、
  test_chunk_graph 365、test_model_tasks 12816 等。
- **性能**（PERFORMANCE.md）：bench 同宿主对比持平/略优（tiles/s 179.5 vs
  154.6；cold load 30.9 vs 35.4 ms；peak RSS 均 88 MB）。未实施热点优化
  （无证据不改性能路径）。
- **独立 Review**：R1 BLOCK（1 P1 + 1 P2 + 4 P3）→ 全部修复并复验；
  R2 **PASS**（3 P3：1 修复、1 记录为已知限制、1 记录更正）。详见
  REVIEW_ROUND1.md / REVIEW_ROUND2.md。

## 未决 / 限制（不虚构完成）

1. **B1（HTTP provider 无 dispatcher 线程）未修**：R14-B 已把永久挂钟改为
   typed refusal；helper-thread exchange 是更大的生命周期改动。已记录为候选
   后续；当前所有真实入口的错误消息指出内部线程原因（C16 的 B2 修复改善了
   provider 缺失类消息，但 HTTP-dispatcher 类消息仍待后续）。
2. **F3-chunk（fence 晚于 stage）未修**：并发同路径运行在 Windows 上响亮失败
   而非撕裂；改动触及 4 条 lane 发布骨架，风险/收益不划算。已记录。
3. **F10（eviction 在 registry 锁内销毁 session）未修**：纯延迟问题。已记录。
4. **B6（commonEstimate 漂移）、B11（env-doctor 不覆盖模型栈）**：属
   `src/operators/rs/**` 与 `src/cli/**` 他轨所有权，已记录给 owner。
5. **VRAM 投影的两处拼写缺口**（review R2 P3）：类型化 reason 留待后续。
6. **NOT VERIFIED**：CUDA/TRT/OpenVINO/ORT 执行车道（无依赖与硬件）；Linux；
   test_device_planner 本宿主挂起（master 同样挂起——预先存在的环境问题，
   其 VramLedger 契约由本轨新用例覆盖）。

## 与 open PR 的去重

- #1514（open）：文件零重叠。#1513（open draft）：唯一共享文件
  `tests/CMakeLists.txt`——本轨注册块在中部（~3044 行），#1513 追加块在尾部
  （9818+），git 可干净合并；未 cherry-pick。
- #1505/#1508/#1509（merged）：未重写其修复，仅回归验证。

## 文件清单

src（18 文件）：`src/operators/framework/{model_catalog,chunked_run}.cpp`、
`src/operators/runtime/{model_publish,model_runtime,model_execution_service,
tile_inference_engine,model_ensemble,detection_postprocess,device_planner,
python_worker_provider,openvino_provider,tensorrt_provider}.{cpp,h}`、
`src/runtime/chunk/{chunk_graph,chunk_pipeline}.{cpp,h}`、`memory_planner.h`。
tests（10 文件）：6 个新 `test_r15_C_*` + 4 个既有文件追加用例 + CMakeLists。
docs：`.planning/r15-c/**`（10 个 markdown）+ `.gitignore`。

## 推送 / PR

见文末（建 PR 后回填 URL）。未自动合并；未等待线上 CI。
