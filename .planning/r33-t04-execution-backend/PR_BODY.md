# R33-T04：执行、工作流、验证与教学后端可靠性批次

## 原问题与影响

执行/验证/教学链上 11 项已确认缺陷：短预算 job 被字典序饿死（#2122）、
workflow 重试身份丢失致管线不收敛（#2123）、缓存服务丢/留 sidecar（#2124）、
畸形 worker 帧以未捕获异常泄漏容量槽（#2125）、change mask 终态与 journal
标记脱节需手工恢复（#2104）、CLI workflow 对类型错误文档崩溃 exit 3（#2110）、
verify 数组证据无法往返（#2115）、教学 passport 类型 fail-open 与百分比
cast UB、verdict stamp 双开文件/全量内存、反馈行丢失不可用原因、批次 resume
进度不单调（#1953）、实验指标损坏静默成 0 与等价运行部分结果当完整集（WP16）、
ScienceContext 缓存键分隔符碰撞（WP17）与 bundle 准入类型不一致（WP18）、
i18n gate 后端三文件超 cap（#2108）、dev 测试套件依赖用户 git 身份（#2113）。

## 基线与增量

- base：`origin/master` `1dfb04382e`（#2139 顶部；#2134–#2137 已在基线合并）
- 本 PR：{{COMMITS}} commits，34+ 文件。已继承 #2071/#2061/#2132/#2133 等全部
  已合并基线，不重复实现。

## 主要行为变化（按链）

1. **调度/重试**：JobEngine watchdog 按真实最早 deadline 取最小（不再
   begin() 字典序）；retryTask 绑定真实状态并补发带 pipeline/step 身份的
   快照——快任务在提交返回前完成时 run 亦收敛；stepStatuses 不再写过时
   Queued。
2. **缓存交付**：sidecar 命名收敛到 `ArtifactGC::sidecarPathForMain` 单一
   权威（stem 规则 + PAM 全名例外）——缓存命中携带真实 .shx/.dbf/.prj/.cpg
   并清除目的地陈旧 sidecar。
3. **Worker 容量**：`frameMatchesOpShape` 逐 op 类型准入（非字符串
   jobId/message/kind、非数值/非有限 progress value → typed
   FrameRejected）；runOnWorker 全程 catch(...) 网兜——任意逃逸（含用户
   onProgress 抛出、bad_alloc）恰好一次 retire，容量可恢复；FrameRejected
   与 crash 分别计数。
4. **change mask 终态**：journal PUBLISHED 标记经 `verifyPublishedOutput`
   绑定最终交付物——mask 丢失/异参/未完成时自动降级重建（tiles 复用），
   不再要求手工删 journal；同一自愈契约也覆盖 spectral 输出。
5. **文档契约**：workflow schema+engine 双层拒绝 mistyped 根 id/title/name
   与 inputs 两种方言的成员类型——validate 与 run 一致返回
   ValidationFailure（不再 LogicError exit 3 / 静默 default 坏 id）。
6. **verify 往返**：evidence observed/expected 接受 object-or-array 闭集，
   state/relational/cross-output 三类报告 serialize→parse→re-canonicalize
   字节与 digest 稳定。
7. **教学/实验**：passport ok 显式四态（true/false/缺失/类型错误→unknown，
   绝不 fail-open）；百分比先 double 域钳制再转型（1e300 不再 UB）；
   verdict stamp 单遍流式摘要（同窗口 size+digest，内存有界，缺文件=
   无效 stamp）；grader.overall 不可用原因写入存储行；批次进度以采纳数为
   基线单调（全采纳 resume 也发完成刻度）；损坏指标 typed 拒绝（不再静默
   value=0）；equivalentRuns 页失败→整体失败（不再部分集当完整集）。
8. **ScienceContext**：缓存键长度前缀（`goal="a|b",intent="c"` 与
   `goal="a",intent="b|c"` 不再互撞）；bundle 准入类型一致（集合必须数组、
   条目损坏 typed 拒绝、execution_blocked 坏类型 fail-closed 为 blocked、
   负预算钳 0）。
9. **i18n 后端 cap**：capability_router 66→0（意图词典移入 .inc 数据包含
   文件——中文为匹配词汇，数据非 UI 文案，编译期逐字节不变）；
   lab_feedback_projection 14→13、lab_session_state 21→15、lab_readiness
   保持 21=allow；总量 nonui_zh 1088→1022 ≤1059。**未抬任何 cap/ceiling**。
   残余 hard_en=1 属 src/app/panels/data_manager_panel.cpp（#2132 引入，
   T06 写集）——已交接（.planning HANDOFF）。
10. **dev suite**：fixture 子进程恒定身份+隔离配置——无全局 git 身份环境
    76/76 全绿（#2113 精确环境本机复现）。

## 接口兼容

- `MetricResult::fromJson` → `Result<MetricResult>`、
  `ReplayReadiness::equivalentRuns` → `Result<QStringList>`：仓内全部调用方
  （CLI/agent 工具/2 测试）已适配；无外部插件面（SDK 未见暴露）。
- 其余改动无公共签名变化；worker 协议对合法帧零变化（仅新增类型拒绝）；
  verify canonical 形状仅放宽（旧拒绝路径变接受），digest 不变。

## 本地验证

- 构建配置：`dev-default` Debug，双 resource_guard（host+build dir），-j4
- 新增回归：10 个 test_r33_t04_* 目标（{{CASE_COUNT}} 个 CTest case）
  - `ctest -R "test_r33_t04_" …`：{{PASS}} 通过 / {{FAIL}} 失败 / 0 跳过
- 直接受影响旧测试：{{REGRESSION_LIST}}
- i18n gate：SELFTEST PASS；GATE 仅剩 hard_en=1（T06 文件，交接在案）
- dev suite：`python3 -m unittest discover -s scripts/dev/tests` →
  Ran 76 tests OK（本机无全局 git 身份）
- RED→GREEN 证据：{{RED_GREEN_NOTES}}

## E2E 验收链

- E2E-A（deadline→retry→workflow 恢复）：test_r33_t04_job_deadline +
  test_r33_t04_retry_identity（真实 JobEngine/TaskCenter/
  WorkflowRunCoordinator，含提交返回前完成）
- E2E-B（坏 worker→容量回收→下一正常作业）：test_r33_t04_worker_admission
  （容量 1 真实池 + 真实 helper 进程，typed 拒绝/异常回调/恢复）
- E2E-C（缓存→产物→恢复→发布）：test_r33_t04_sidecar_naming +
  test_r33_t04_change_mask_recovery（journal 自愈 + 三跑幂等）+
  test_r33_t04_spectral_claim_contract（伪造 stamp 重建）
- E2E-D（verifier 报告→持久化→投影）：test_r33_t04_verify_roundtrip（真实
  evaluate → canonical → parse → digest 复验）
- E2E-E（实验教学/ScienceContext 恢复）：test_r33_t04_experiment_admission +
  test_r33_t04_context_cache_key + test_r33_t04_bundle_admission +
  教学四测试（readiness/feedback/stamp/batch）

## 限制与环境通道

- WP08（#2116，归档 src/verification 的预算降级投影）：未实现——生产 core
  无关，参考层修复未纳入本批（诚实记录）。
- WP11 深度内容证明（stat hint→内容 hash）：需 T03 身份接口，仅 handoff；
  本批闭合 sidecar 集合（WP03）与 claim 数据契约（WP10）。
- WP19（制图 adapter outputs 声明）：未实现（需 T06 真实服务 fixture/可见
  UI 验收交接）。
- Windows 通道（UTF-16/长路径）：本机 linux；未执行 Windows 复验
  （ENV_BLOCKED，如实标注）。
- 构建锁为六轨共享，本轨验证均在获取锁后串行完成。

## 审查

- R1（独立子代理，读全量 diff+调用方+测试）：{{R1_SUMMARY}}
- R2（独立复查修复与最终 diff）：{{R2_SUMMARY}}

Refs #2122 #2123 #2124 #2125 #2104 #2110 #2113 #2115 #2108 #1953
