# R30-T05 BASELINE — 基线证据（base 606e3c9101, 2026-11-14）

## 仓库状态

- origin/master = `606e3c91017d8dc3d3ad363e613065ef214a82c6`（PR #2053 已合；快照 dcda90bf00 之后的 #2045–#2053 均为 int-range/telemetry/pool 小修）
- Open PR：0。Open issues（非 PR）：48，其中本轨相关 rollup：#1931 [agent] 119 条、#1932 [agent-ops] 12 条、#1952 [sdk] 23 条、#1802 [agent-security P3]、#1628 [src/agent 技术债 5 项]。
- R29-T05 本轨前作 = PR #1827（R29-T03 pi-mcp 轨）。其后相关合入：#1999 journal 读后上限、#2007 delivered stage、#2020/#2034/#2042 分页/整数边界。

## Pi 基线（本 worktree 实测）

`node --test --test-concurrency=1 pi/test/*.test.mjs` → **66 tests / 65 pass / 0 fail / 1 skip**（skip = r25_c_e2e_real_host 缺 EXP_RS_MCP_BIN）。与任务快照一致。

## 候选缺口判定（按任务第六节六条证据）

### 1. drift 检测没有约束派发 — CONFIRMED_OPEN（代码级）

- `pi/exp-rs-spatial.ts:59-67` catalogDrift 只记账；`:370-378` execute 闭包直接 `requestOrAbort`，无 gate。
- `pi/test/r29_t03_schema_drift.test.mjs` 只断言 status 报告，不验证 stale dispatch 被拒。
- 复现计划：r30_t05_drift_dispatch.test.mjs（fake server + crash + tools-file 改写）。

### 2. wait 的错误/deadline/abort 缺口 — CONFIRMED_OPEN（代码级）

- `pi/exp-rs-spatial.ts:298-301` status request 未传 signal；`:302-308` 未查 isError，malformed 一律继续轮询；
- `:278` timeout_seconds 无有限性/范围校验（NaN/Infinity → 死轮询，负数 → 立即超时）；
- `:309-314` 终态返回前不复查 abort（abort 已发 cancel 后迟到 completed 仍作为成功返回）；
- `:315` deadline 检查不含在途 request（一次 10min 的 status request 可越过预算）；
- sleep 期间 abort 只能在 loop-top 察觉。
- server 侧 status 词汇：completed/failed/canceled/running（mcp_server.cpp:375-391）；get_execution_status 在 poller 黑名单（:1208），cancel 通知对 poll 无副作用 → 可以安全地用 deadline 联动 abort 停止在途 poll。

### 3. 大结果 continuation 不可执行 — CONFIRMED_OPEN（代码级）

- `pi/exp-rs-spatial.ts:151-159` 过长文本只给通用 artifact_read 文案，无真实 path/next_offset。
- server `handleArtifactRead`（mcp_server.cpp:1452-1623）本身契约完备：256KiB 页、UTF-8 边界剥离、next_offset 严格前进、整文件 sha256。但 QVariantMap 键序 = 字母序（content 最前），text JSON = `{"content":"<huge>",...,"path":...,"sha256":...}`；Pi `truncateTail` 保尾 → 尾部 path/next_offset/sha256 文本仍在，但 JSON.parse 失败 → details 全丢。
- 服务端 spatial 工具 512KiB compaction（spatial_tool_registry.cpp MeteredTool）elide 为 note 信封，无落盘 → 无 path 场景必须诚实说"缩小查询/分页"，不得伪造 path。
- 修复方向：structuredDetails 对被截断文本做**有界尾部信封提取**（path/sha256/size_bytes/next_offset/offset/truncated/length/encoding 全部出现才认定 artifact 信封），details.artifact + 可执行 continuation（真实 path+offset+length≤预算建议）；structuredContent 镜像加 50k 预算防绕过。

### 4. 持久会话保守边界 — 现状为契约（不是缺陷）

- `agent_ops/resume_reconciler.cpp` 只允许 pre-plan 两阶段恢复；`mcp_server.cpp:3761-3798` UNKNOWN_SESSION 指向 reconcile。WP06 只做 journal 有界读取 + 诊断一致性，不放宽。

### 5. journal 读入有界性 — PARTIAL（#1999 修了拒绝，未修分配）

- `agent_loop/session_journal.cpp:311-320` stat 检查、`:329-331` `buffer << in.rdbuf()` 全量读、`:334-341` 读后检查。growing-file 场景：stat 时合法（≤cap），读时已长到任意大 → ostringstream 先完整分配再拒绝。WP06：改为最多读 cap+1 字节即停。

### 6. 会话表/worker 生命周期 — 需实测（WP05）

- `mcp_server.cpp:3555-3620` worker 循环、`:3622-3661` submit（slot 占用→BUSY）、R1-P1A/B/C/P2D/P3 修复已在。边界验证留给 test_mcp_server 现有用例 + E2E，不预设缺陷。

## 构建/测试入口（本 worktree 实测）

- configure：`cmake --preset dev-default -DOpenCV_DIR=/home/kevin/projects/rs-studio/main/.scratch/ui-build-deps/opencv/usr/lib/cmake/opencv5`（系统 Qt6/GDAL；OpenCV 为共享第三方依赖前缀，非他轨构建产物）
- build（进行中）：targets = sicnu_geo_rs surface_mcp_host test_mcp_server test_r29_t03_production_driver test_agent_loop_core test_agent_loop_journal_durability test_agent_loop_resume test_agent_ops_core test_agent_ops_driver test_cli_agent_ops test_agent_session_adapter，双锁 -j4。
- Pi 套件：`node --test --test-concurrency=1 pi/test/*.test.mjs`。
