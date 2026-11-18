# ISSUE_TRIAGE — r33-t04-execution-backend（基线 1dfb04382e 上的本轨处置）

状态机：CONFIRMED_RESIDUAL(静态确认且未修) / IMPLEMENTED(本轨已修+测试) /
PARTIAL / NOT_APPLICABLE / OUT_OF_SCOPE / ENV_BLOCKED

| Issue | 标题要点 | 基线复核 | 本轨处置 |
|---|---|---|---|
| #2122 | JobEngine deadline 按 jobId 字典序 | HEAD 确认（begin() 当最早） | **IMPLEMENTED** WP01（min-scan + 4 case 真实引擎测试） |
| #2123 | retryTask 先调度后绑身份 | HEAD 确认（submit→flush→bind；coordinator 丢弃无身份更新） | **IMPLEMENTED** WP02（实际状态镜像 + 绑定后补发带身份快照） |
| #2124 | serveFromExecutionCache sidecar artifact+suffix | HEAD 确认（真实规则=stem；PAM=全名） | **IMPLEMENTED** WP03（ArtifactGC::sidecarPathForMain 单一权威 + 测试） |
| #2125 | worker 帧仅查 op 存在；异常绕过回收 | HEAD 确认（parseFrame 不查类型；LogicError 逃逸泄漏 slot） | **IMPLEMENTED** WP04+05（frameMatchesOpShape + FrameRejected + catch 网兜 + 真实 helper 进程测试） |
| #2104 | change PUBLISHED 标记在 magnitude 后即提交 | HEAD 确认（marker 属 magnitude；mask 失败后要求手工删 journal） | **IMPLEMENTED** WP09（verifyPublishedOutput 绑定终态：mask 缺失/异参→降级重建自愈） |
| #2110 | CLI workflow run 非法类型 LogicError exit 3 | HEAD 确认（schema 不查根类型/inputs；engine 静默 default） | **IMPLEMENTED** WP06（schema+engine 双层类型拒绝；validate/run 一致 ValidationFailure） |
| #2113 | dev suite 无全局 git 身份 3/76 失败 | 本机复现（NO_GLOBAL_NAME/EMAIL） | **IMPLEMENTED** WP20（子进程恒定 fixture 身份+隔离配置，76/76 全绿） |
| #2115 | verify 数组 evidence 无法往返 | HEAD 确认（fromCanonicalJson 仅收 object；三类检查被拒） | **IMPLEMENTED** WP07（object-or-array 闭集 + 真实 evaluate 往返/digest 稳定测试） |
| #2116 | 参考verifier预算超限渲染为 pass | 归档 src/verification 的 render_teaching/render_agent | **PARTIAL→未动**（WP08：参考层投影一致性未实现，见 PR 限制节；生产 core 无关） |
| #2108 | i18n gate 5 条失败 | 本机复现（hard_en1 + 总量1088>1059 + 3 文件 cap） | **IMPLEMENTED(后端部分)** WP18：3 cap+总量全绿；hard_en 归 T06（HANDOFF） |
| #1953-教学残差 | readiness 类型 fail-open；百分比 cast UB；stamp 双开/全读；反馈行丢原因；批次进度 | 逐条 HEAD 复核 | **IMPLEMENTED** WP12/WP13/WP14/WP15（各配测试） |
| #1953-其余 11 条 | swarm P2/P3 汇总其余项 | 未逐条展开 | NOT_TRIAGED（超出本轨已选 20 WP 范围，PR 中如实说明） |

## 动态复现记录（RED 侧）

- #2110：issue 报告者已给动态复现（exit 3）；本轨静态确认同根因后修复；
  schema/engine 层测试固化（CLI 动态 RED 复验受构建锁限制，以层测试为准）
- #2113：本机精确复现环境（无全局身份）修复前 3 失败（历史快照记录）→修复后 76/76
- #2108：audit 脚本本机动态复现（修复前 hard_en1/总量1088/66/14/21 → 修复后 1/1022/0/13/15）
- 其余静态确认项以新增行为回归测试（GREEN 侧）+ 独立 review 验证
