# R20-03 BASELINE — Pi/MCP Harness、工具契约、Workflow 与插件执行闭环

Live recon 执行时间：2026-10-22/23（会话内实时勘查，非快照推断）。

## 1. LIVE 仓库状态
- `origin/master` = `28e1891390bb812dc7ef2fe045ac90492c9525f1`（与审计快照一致；`git fetch --prune` 后无新提交）
- PR 状态：#1662–#1667 全部 MERGED；**无 open PR**；远程仅 `master` + 历史 agent/hardening 分支（无 origin 端新 worktree 分支）
- Open issues：126 条（gh 全量遍历）；本轨相关分诊见 ISSUE_TRIAGE.md
- 本轨 worktree：`/home/kevin/projects/rs-studio/exp-rs-agent/r20-03-pi-mcp-workflow-20261022T160809Z`（分支 `agent/r20-03-pi-mcp-workflow-20261022T160809Z`，基于 origin/master 28e1891390）
- 并行轨实时状态：R20-01（science algorithms）正在构建；R20-02（data/model/runtime）刚在自有 worktree 重新 configure。均使用同一全局锁 `/home/kevin/projects/rs-studio/main/.git/exp-rs-zcode-global-build.lock`。

## 2. 核心链路（继承 R19-02 BASELINE 并逐项 live 复核）
- `pi/exp-rs-spatial.ts` → `pi/mcp_bridge.ts`（代际化子进程、4MiB 行界、EOF 半帧、>32MiB runaway kill、checkpoint poison、nextId 跨代单调）→ 宿主二进制 `--mcp`（`src/agent/mcp_server.cpp`）。
- mcp_server：StdinReader 干净 EOF gcount 区分（#1628-1 已修 live 复核）；tools/call arguments 非对象 → -32602（live 复核 :856-866）；validateWorkspacePaths 自由文本豁免（#1628-2 已修）；rpcCancelMapKey 类型化。
- `src/workflow/workflow_runtime.cpp`：runStep 入口 m_mutex 内消费陈旧取消 + requestCancel 同锁 store（#1654 残余竞态已修，live 复核注释明示 R19-02）。
- `src/agent_ops/`：#1539（裸布尔审批空转）已修——readApprovalArgs 拒绝 `approve_pending_repair`、走 repair_approval+approval_now_ms 令牌路径（live 复核 ops_driver.cpp:40-66）；#1540（跨线程审批竞争）已修——dock 改为 worker 线程内 arm + 单线程亲和契约（live 复核 agent_ops_control_center_dock.cpp startSession）。
- `src/agent_loop/session_journal.cpp`：bounded 4096 条 + goal_normalization 钉定驱逐 + stackLimit=128 + catch + fail-closed load（live 通读复核，无新缺陷）。
- checkpoint sink 安装已提取为 `installCheckpointSink` 共享助手且注释准确（#1629-3 已修，live 复核 :115-146）。

## 3. 环境
- Arch CachyOS x86_64、GCC 16、cmake + Unix Makefiles、Debug、QT_QPA_PLATFORM=offscreen。
- **R19-02 的 vendor 前缀（/home/kevin/project/paleo-singlefactor-closure/...）已消失**；本轨复刻 R20-02 的纯系统依赖配方：`cmake -S . -B build-r20-03 -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON`（GDAL/PROJ/GEOS/GSL/NETCDF/QCA/Qt6/SQLite3 全部 /usr 解析，configure exit 0，100s+30s）。
- 全局构建锁：`/home/kevin/projects/rs-studio/main/.git/exp-rs-zcode-global-build.lock`（-j2；与 R20-01/R20-02 串行）。
- Node v24.21.0：`node --test pi/test/` 目录形式在 Node24 下报 MODULE_NOT_FOUND，须用 glob `node --test "pi/test/*.test.mjs"`。

## 4. master 基线测试证据（本轨域）
- pi Node 套件：**39/39 GREEN**（glob 形式，9.6s）——含 surface_snapshot、bridge_parity、no_drift、r18_c 两个故障注入、knowledge 预算门。
- 其余 C++ 族待构建完成后 ×2 采集（见 TEST_RESULTS.md）。

## 5. 前驱（R19-02/#1666）遗留的已知状态
- CT-6：7 个 cartography 工具 raw schema 缺 `properties:{}`（4 个）/`type:"object"`（3 个）→ **04 轨红区**，本轨只读证据 + KNOWN_BASELINE_FAIL；修前 surface_snapshot registry 腿确定性失败。
- plugin_host 进程族测试 R19-02 NOT_EXECUTED → 本轨补执行（C09 验证债）。
- 死字段候选：`operations_coordinator.h:40 approvePendingRepair`（#1539 修复后零读零写）。
