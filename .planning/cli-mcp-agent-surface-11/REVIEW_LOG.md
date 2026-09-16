# REVIEW_LOG — cli-mcp-agent-surface-11

## Reviewer #1: 主代理自 review（全 diff origin/master...HEAD）

| # | 严重度 | 发现 | Disposition |
|---|---|---|---|
| R1-1 | P3 | artifact_read offset 负数静默 clamp、小数 toLongLong 隐式取整 | accepted（宽松输入处理与 master 其它工具一致；边界仍封闭） |
| R1-2 | P3 | QCryptographicHash::addData / QVariant::type() 弃用告警 | accepted（master 同款告警，非本 track 引入的行为问题） |

## Reviewer #2: 独立对抗 review（只读子代理，全 diff + 不变量验证）

验证确认（未破坏的不变量）：meta 表搬运逐字节语义无损（251 个字面量逐一比对）；
surfaceIdAllowed 与 master verbatim；tools/list 线序/过滤/字段集不变；progress 中继
终态唯一 + map 有界 + 跨线程投递安全（flushPendingSignals 在 mutex 外发射）；
artifact_read 对 symlink/file:///vsicurl//dev/zero/FIFO 的针对性攻击全部闭环；
redaction 正则无灾难性回溯；CMake 共享文件 append-only；E2E 两遍设计有效。

| # | 严重度 | 发现 | Disposition |
|---|---|---|---|
| R2-P1-1 | P1 | 测试依赖机器本地 /tmp fixtures（非 hermetic） | **已修**：ensureFixtures() 自建 fixtures；QFile::copy 前先 remove（copy 不覆盖） |
| R2-P1-2 | P1 | batch envelope 泄漏未 redact 的 error（注释与实现不符） | **已修**：setError() 单一边界，record 创建时即 redact；index 与 envelope 同源 |
| R2-P1-3 | P1 | policy.on_error 非 string 抛 Json::LogicError → SIGABRT | **已修**：isString() 守卫 + 负样本测试 |
| R2-P1-4 | P1 | manifest 根级未知 key 静默忽略（违反 strict 契约） | **已修**：根 key 白名单 {version,variables,policy,tasks} + 2 个负样本 |
| R2-P1-5 | P1 | "Errors are redacted" 测试空泛（删掉 redact 也过） | **已修**：operator id 携带 password=hunter2，断言 [REDACTED] 存在且 hunter2 不存在 |
| R2-P2-1 | P2 | get_tool_help 可 dispatch 但不在投影（gate 宣称覆盖的漂移类别真实存在） | **已修**：get_tool_help 加入 meta 表（追加在尾，既有行序不变），投影/dispatch/回退三方一致 |
| R2-P2-2 | P2 | batch validate 有 latent 失败仍 exit 0 | **已修**：validate 聚合最劣 exit code + problems 数组 |
| R2-P3-2 | P3 | 订阅 map 逐出可能选中刚插入的 key | **已修**：victim 跳过新 key |
| R2-P3-3 | P3 | 进度 >100 会发出越界 MCP progress | **已修**：qBound(0.0, x, 1.0) |
| R2-P3-6 | P3 | E2E 继承环境的 SICNU_MCP_WORKSPACE 会误杀 artifact leg | **已修**：QProcess 环境剔除该变量 |
| R2-P3-9 | P3 | jsonLines/json 分支重复 | **已修**：合并 |
| R2-P3-11 | P3 | PERFORMANCE.md 宣称的上界断言与实际不符 | **已修**：文档改为如实描述 RateLimiter 单测的三因归因 |
| R2-P1-2 追加 | P1（review 后补强） | index/envelope 的 operator 字段也是 caller input 回显 | **已修**：operator 字段过同一 redactText 边界（合法 id 原样通过） |
| R2-P3-1 | P3 | run_workflow 的 _meta.progressToken 被忽略（多任务）；提交期事件可能错过 | accepted，follow-up（run_workflow 每 step 一订阅需要 pipeline 级映射，属 workflow 域接口扩展） |
| R2-P3-4 | P3 | 大文件整读 sha256 的 IO 成本 | accepted（正确性优先；文档已注明，digest 缓存列为 follow-up） |
| R2-P3-5 | P3 | offset 宽松解析 | accepted（同 R1-1） |
| R2-P3-7 | P3 | scale 测试中途失败会泄漏 2000 probes | accepted（失败即整 test binary 失败，泄漏不影响其它 binary） |
| R2-P3-8 | P3 | batch 人类模式静默 | **已修**：非 --json 模式输出每任务一行摘要 |
| R2-P3-10 | P3 | SurfaceQuery::includeGuiOnly 无调用方 | accepted（GUI 消费方的显式开关，留给 app 层） |

修复后全量重跑：test_cli_batch_manifest 10/10 (99) · test_surface_protocol 10/10 (70) ·
test_surface_parity 8/8 (4248) · test_surface_e2e 4640 assertions（场景×2）——全部 exit 0。

**最终：P0=0 P1=0**（P2 全部修复；P3 修复 6 条、accepted/disposition 5 条，逐条如上）。
