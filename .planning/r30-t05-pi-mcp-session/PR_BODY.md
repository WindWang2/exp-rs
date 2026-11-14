# R30-T05: Pi/MCP 与 Agent 会话 — drift 派发约束、wait 语义闭环、有界 continuation、journal 有界读取

## 原问题与影响（#1827 之后的剩余缺口）

R29-T03（#1827）建立了 Pi 桥接的 drift 检测、错误透传与分页协议，但四个用户可观察缺口仍在：

1. **drift 只报告不拦截**：服务器重启后 schema 变化的工具仍可用旧 Pi 端 schema 静默调用（`pi/exp-rs-spatial.ts` execute 闭包无 gate）——错误参数语义下会静默误执行。
2. **wait 工具语义洞**：`timeout_seconds=NaN` 死轮询；总预算不含在途请求（迟到 completed 冒充成功）；abort 后迟到终态复活已结束的 waiter；`get_execution_status` 的 typed error 被吞成 "unknown status" 轮询到超时；malformed 响应无限轮询。
3. **大结果 continuation 不可执行**：>50k 截断结果给一条泛称 artifact_read 的提示——没有真实 path/cursor/checksum（server 端 512KiB elide 不落盘时该提示指向不存在的文件）。
4. **journal 读取分配无界**：#1999 只在读后检查大小，stat/read 之间增长的文件先完整分配再拒绝。

## 已有实现基线（#1827 / #1957–#2053 保留，无回退）

productionAgentOpsDriver 唯一装配、serialized session worker、UNKNOWN_SESSION→reconcile、journal 保守 resume 边界、512KiB compaction、artifact_read 分页契约全部保留并经现有回归验证（见验证表）。

## 本次增量

- **dispatch gate（WP02）**：per-generation comparison promise + comparedGeneration；changed/removed 工具拒绝（`SCHEMA_DRIFT`，不可重试，指引重启）；comparison 未落定有界等待（5s）、失败每次调用重试一次后 `SCHEMA_UNVERIFIED`（可重试）；穿越 respawn 边界的调用重验（crash 类错误重 gate 重发 ≤2 次、成功响应事后裁决）。unchanged 工具不受影响；schema 恢复注册版本后自动恢复派发。
- **wait 闭环（WP03）**：`timeout_seconds` 有限性校验；预算联动在途 poll（`AbortSignal.any`，get_execution_status 在 server poller 黑名单、取消通知无副作用）；abort 切断 sleep/poll、终态返回前复查 abort（迟到终态如实记录不冒充成功）；isError 透传 errorCode/errorCategory/retryable；malformed/未知状态 3 连击快败；`cancelling` 为非终态；timeout 只停止等待不取消执行（写入工具 description），abort 请求服务端取消（"requested"≠"canceled"）。
- **可执行 continuation（WP04）**：截断文本尾窗扫描提取 artifact 信封（path+sha256(64hex)+size_bytes 三元组门槛；JSON 转义使嵌入载荷不可伪造）→ `details.artifact` + 真实工具名/path/resume offset/预算内页长的 continuation；无 artifact 的截断结果诚实提示缩小查询；structuredContent 镜像 4096/串 上限防绕过 50k 预算。不新建落盘 spill（现有 elide+分页已覆盖真实场景）。
- **journal 有界读（WP06）**：load 按 64KiB 分块读至 cap+1 字节为止（分配有界，growing-file 场景不再先吞再拒）；at-cap/cap+1/跨 128 块三用例钉住边界。
- **session 表边界（WP05）**：68 次启动钉住 64 条 FINISHED 保留上限（最旧驱逐、被驱逐 id → UNKNOWN_SESSION+reconcile、最新 id 可解析、id-less status=最新 final envelope）。
- **测试基础设施**：fake MCP server 新旗标（delay/fail-once tools-list、status-script、call-log）；surface host 修复 GDALAllRegister 缺失与 SleepOperator 取消契约（throwIfCancelled，见下）。

## 与其他轨边界

- 执行/任务引擎归 T04：取消信号契约的引擎侧事实已写入本轨 `.planning/.../HANDOFF.md`（fixture 层已修，引擎未动）。
- cartography/verifier/教学/数据目录未触碰。`tests/CMakeLists.txt` 仅追加本轨小块；`test_mcp_server.cpp` 仅追加用例（本轨模块）。

## 真实验证（本 HEAD，dev-default -j4 双锁）

- Pi 套件 **90 tests / 90 pass / 0 fail / 0 skip**（多遍一致；基线 66/65/0/1 的 skip = 真实 host 链，本次构建后首次全部运行并通过，含 r25 crash→diagnose→resume）。
- 新增：drift 派发 8（含 R1 补的 crash-boundary 两例）、wait 语义 8、continuation 4、journal 边界 3（C++ 13 断言）、session 表 evict 141 断言、E2E E1/E2/E3 共 4。
- C++ 回归（直接相关 + 边界验证）：test_agent_loop_{core 216/17, journal_durability 71/4, seams 101/10, modes 110/11, e2e 150/15}、test_agent_ops_{core 491/47, driver 62/5}、test_cli_agent_ops 61/5、test_agent_session_adapter 10/2、test_mcp_server **10104/33**、test_r29_t03_production_driver 86/7、test_r27_t03_{approval 214/4, dock 12/6}、test_agent_ops_ui_smoke 12/5、test_r25_c_{capability 198/1, resume 95/4}、test_r28_t03_{seams 70/7, verification 17/1, shm 27/2}、harness_{adversarial 88/16, evidence 71/8}、plugin 全家 {loader 566/38, host_process 371/34, parity 10/3, unload 34/4, barrier 23/6}。
- 真实 host E2E：E1 真实结果链（栅格 inspect→执行→wait 终态→>150KB CJK artifact 分页重组 sha256 一致）；E2 abort 服务端真取消/timeout 不取消/host 存活；E3-A kill→gate→checkpoint→resume；E3-B 生产 `sicnu_geo_rs --mcp` UNKNOWN_SESSION + pre/post-plan reconcile 判定。

## 接口变化

- 新 typed 错误码（Pi 侧）：`SCHEMA_DRIFT`（不可重试）/`SCHEMA_UNVERIFIED`（可重试）。
- `exprs_wait_for_execution`：非法 `timeout_seconds` 立即拒绝；timeout 不再隐含任何取消语义（契约显式化，原实现亦不取消，行为不变仅文档+一致性修正）；malformed 状态 3 连击后失败（原为轮询到超时）。
- `details`：截断结果新增 `details.artifact`（可选）；`resultContinuation` 文案变化（旧文案对无 artifact 结果承诺了不存在的恢复路径——缺陷修复）；structuredContent 镜像超长字符串截断（当前 server 不发送 structuredContent，无实际消费者受影响）。
- `exprs_status` details.drift 新增可选 `comparisonGeneration`。
- 兼容性：两个 R29 测试按新契约更新（详见 commit message）；无公共 C++ API 变化。

## 限制与未执行

- 逐字节"发送前"gate 需要 bridge 侵入式 API，未实现；当前覆盖 crash 窗口事后裁决（DECISIONS D1 记录）。
- E4 的 Control Center 交互经既有 dock/ui-smoke/production-driver 回归覆盖（offscreen），未做人工 GUI 操作；插件链经 R4 全家回归覆盖。
- Windows/macOS、GPU/TensorRT、OTB/ITK、sanitizer 通道未执行（本轨变更不需要）。

Refs #1827（能力基线）。独立 review 记录：`.planning/r30-t05-pi-mcp-session/`（R1/R2 结论见 REVIEW_*.md）。
