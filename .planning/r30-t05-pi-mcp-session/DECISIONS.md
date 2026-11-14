# R30-T05 DECISIONS — 自主决策记录

## D1: drift 派发 gate 的状态模型（WP02）

- `catalogDrift.comparison`（per-generation settle promise）+ `comparedGeneration`（落定代数）。
- gate 语义：gen1 直通（注册即 catalog）；gen≥2 必须等当前代 comparison 落定才可派发；changed/removed → `SCHEMA_DRIFT`（不可重试，重启恢复）；pending/失败 → `SCHEMA_UNVERIFIED`（可重试，附原因）；失败 comparison 每次 gate 调用重试一次（瞬态 re-list 失败可恢复）。
- 有界等待 5s（本地 handshake 的 re-list 是毫秒级；容忍慢盘/忙服务器，不让调用无限挂起）。
- **崩溃窗口**：调用穿越 respawn 边界（gate 时 gen=N，实际 gen=N+1 服务）由 execute 外层循环处理：crash 类错误（CHILD_EXITED/RESTART_FAILED）+代数变更→重 gate 后重发（≤2 次）；成功响应但代数变更→事后 verdict，drifted 则拒绝（明示调用可能已到达服务器）。这是在 Pi extension API 下能做到的最严格约束；逐字节级别的"发送前 gate"需要 bridge 侵入式 API，超出本轨范围（记录为限制）。

## D2: 老测试更新（r29_t03_schema_drift / r29_t03_details_passthrough）

- `schema_drift` "drift measured against REGISTRATION"：原测试用 drifted 工具本身当 respawn 触发器——这正是 WP02 要禁止的 stale dispatch。改为用 unchanged helper 触发，状态断言不变，并补"恢复注册 schema 后派发恢复"断言。
- `details_passthrough` 两个 oversized 测试：原断言无 artifact 的截断结果也提示 `exprs_artifact_read`——任务明确"不要声称泛称 artifact_read 的提示能恢复不存在的文件"。改为断言诚实文案（narrower filter/pagination）且 NOT mention artifact_read。
- 判定：这两处是任务指定的契约变化，不是回归。

## D3: wait 工具语义（WP03）

- `timeout_seconds`：非有限/≤0 → 立即拒绝（NaN 曾导致死轮询）。
- 总预算含在途 request：每次 poll 用 `AbortSignal.any([signal, AbortSignal.timeout(remaining)])` 联动——超预算时 poll 被切断并报 timeout（fake 实测 250ms 预算下 300ms 的 completed 不再变成成功）。abort 只读 poll 安全：server 侧 get_execution_status 在 poller 黑名单（mcp_server.cpp:1208），cancel 通知不会误杀执行。
- abort：sleep 可中断；request 可中断；终态响应到达后复查 abort——迟到终态不复活已结束 waiter，错误信息如实携带"terminal 'completed' arrived after the abort"。
- timeout ≠ 取消执行（现有产品契约，已写入工具 description）；abort = 请求服务端取消（"requested"≠"canceled"，错误信息不伪造 canceled）。
- isError 透传（errorCode/errorCategory/retryable），malformed/未知状态 3 连击快速失败（带最后 payload 头部），`cancelling` 为非终态继续轮询。

## D4: 有界结果 continuation（WP04）

- QVariantMap 键序=字母序 → content 在前、分页元数据在尾；tail-truncation 后元数据仍在文本尾部但 JSON.parse 失败。解决：对截断文本做**有界尾部扫描**（8k 窗口），要求 path+sha256(64hex)+size_bytes 三元组齐备才认定 artifact 信封（单一 path 可能是任意工具字段；宁缺毋假）。
- 嵌入 JSON 不可伪造匹配：JSON 字符串内 `"` 必须转义，未转义 `"key":` 邻接只可能在信封层（测试覆盖 /evil/inside 场景）。
- continuation 消息给出可执行参数：真实工具名 + path + resume offset + 40k length（预算内页大小）+ 整文件 sha256 提示。
- 无 artifact 的截断结果：诚实说"server 512KiB elide 不落盘，缩小查询"，不提 artifact_read（老契约的 bug）。
- structuredContent 镜像加 4096/字符串 上限，防未来 server 启用 structured 绕过 50k 预算（浅层；当前 server 不发送 structuredContent）。
- 不新建落盘 spill：现有 512KiB elide + artifact_read 分页已覆盖真实场景；无真实需求（YAGNI，任务允许）。

## D5: 构建/依赖

- OpenCV 使用主检出的 `.scratch/ui-build-deps` 共享第三方前缀（只读依赖，非他轨构建产物）；Qt6/GDAL 用系统包。未启用 OTB/ITK/GPU/sanitizer（本轨变更不需要）。

## D6: fake_mcp_server 新增测试旗标（测试基础设施）

- `--delay-tools-list <ms>`、`--fail-tools-list-file <path>`（fail-once 语义：marker 消费后恢复）、`--status-script <path>`（顺序状态脚本，末项重复）、`--call-log <path>`（tools/call 记录，用于断言 cancel 恰好一次）。只影响 pi/test fixture，不影响生产代码。
