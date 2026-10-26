# R21-C REVIEW_R1 — 第一轮独立对抗审查（fresh reviewer，独立上下文）

审查对象：`git diff 911e0b17a3...HEAD`（R1 时 11 commits，24 文件，+811/−44）。
审查方式：general-purpose 子代理，独立上下文、主动寻找反例；已实证项含真实二进制运行与 gdb 回溯。

## 发现与处置

| # | 级别 | 发现 | 处置 |
|---|---|---|---|
| R1-1 | **P0** | **workflow resume 双重 takeFirst**：W-A 的门把 run id 先从 args 取出，但 runner 仍消费一个 token → `workflow resume run-x` 在空列表上 takeFirst（Debug 下 SIGABRT/134 无输出；release 下为 UB），`workflow resume a b` 续的是错误的 run。master 单 takeFirst，属本轨引入的硬回归。逃逸原因：resume 族零 CLI 测试覆盖 | **已修**（commit eef09d109e）：runner 改用门的 run-id 局部；补 2 个 section（bare form → typed ExecutionFailure 且点名 run id；typo flag → 6）。实证：`workflow resume no-such-run-r4 --json` → exit 3 + "No checkpoint for run no-such-run-r4" |
| R1-2 | P1 | `data catalog search --platform <value>`：Qt 自有 argv 解析在 QGuiApplication 构造期消费 `-platform/--platform`，值不是可加载平台插件时**直接 abort 进程**（exit 134 无 envelope）；`--platform offscreen` 被静默吞掉。master 同款（非本轨引入），但 W-A/W-B 的 expected 文本已把该 flag 宣传为可用 | **已修**（855fe94580）：仅从 Qt 视图移除该 token（两种拼写 + `--platform=` 形式），dispatch 仍走 originalArguments 快照（与 Track-14 `--name` 同构）。实证：`--platform sentinel-2 --json` → exit 0 + `ok:true` envelope |
| R1-3 | P1 | W-D9 只交付一半：`session approve-repair` 无法铸币——surface 读 now_ms/ttl_ms 而 CLI 无对应 flag，只能 APPROVAL_MINTING_FAILED，本族打印的指路成为死胡同 | **已修**（d18b934291）：新增 `--now-ms`/`--ttl-ms`（整数门，同 wire）。测试实证 flag 被接纳并到达 surface（fresh coordinator → typed NO_PENDING_REPAIR_PLAN 而非 unknown flag） |
| R1-4 | P2 | F1 门是提交者白名单而非轮询黑名单：直连路由的提交者（view:/roi:/canvas:/layer:/raster: 与全部 allowed tool id 经 dispatchToolCall 提交）结果也带 execution_id，其 rpc-id 取消通道与 progressToken 订阅被静默丢弃 | **已修**（dab9020782）：改为轮询黑名单（排除 get_execution_status 与 cancel_execution），所有提交者保留取消通道；mcp_server 9692/32 复绿 |
| R1-5 | P2 | session 族不剥离全局 envelope flag：`session status --json` → unknown flag，脚本化审批流无法要 envelope | **已修**（d18b934291）：takeFlag/extractGlobalFlags 入 cli_commands.h 作 inline（改名 takeGlobalFlag 避开与各命令文件本地 takeFlag 的歧义）；测试实证 `status --json` 正常解析 |
| R1-6 | P3 | catalog 循环不检查值 flag 的值是否为 flag（`--platform --limit 5` → platformEquals="--limit"） | **已修**（eef09d109e）：值与 algorithms search/session 同规则，缺值即 typed 拒绝 |
| R1-7 | P3 | capability_mirror 有界读为每个文件分配 4MiB+1 | **已修**（e15bf98f4a）：file_size 先行，KB 级 sidecar 付 KB 级缓冲；种植文件在任何分配前被拒 |
| R1-8 | P3 | commit 68e6740bf8 消息写 "13 sites" 实为 12 处命名快照 | 记录在案（ cosmetic，历史不改） |

## 审查确认无问题的方面（复核证据）
- **R20 #1670 反回滚**：`git log 911e0b17a3..HEAD -- src/sdk/exprs/ipc_envelope.cpp src/sdk/exprs/ipc_channel.cpp src/agent/harness/capability_*.cpp src/agent/harness/grounding_tools.cpp` 为空——四个修复点原位在位（E6002 result 门 / hexdump 抢救 / decision_record 门 / 三加载器 fail-closed）。
- F2 正确性（absent/null parameters 仍接受；object 不误拒）；F1 测试强度（statusOf 用 rpc 99，断言真实被执行）；F3 首次终态迁移仍记 journal；D5 respawn 换新 session 对象快照不过期；D6 owner 元数据仅诊断用途不动锁语义；F4 pi 41/41；W8 命名快照模式正确且 src/ tests/ 零残留 dangling。
- **所有权**：24 文件全在写区内，零越界。

## 结论
READY-WITH-FIXES → 全部 P0/P1/P2/P3 已修并复验（上表 commit 列）。触发 R2（第二 fresh reviewer，全范围 + 实际执行 + 新增对抗边界）。
