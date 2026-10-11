# R8 Track Oracle — Agent Ops / Workflow / TaskCenter 真实执行闭环

TASK_SLUG: r8-agentops-workflow-execution
BRANCH: hardening/r8-agentops-workflow-closure
WORKTREE: ../exp-rs-r8-agentops
BASE_SHA (S0 snapshot, 2026-10-11): 47c6371f3e19629ecf51f5a3cc9328e5b75f9869 (origin/master)

## 状态矩阵（S0 实测）
- OPEN_PR_MATRIX: PR #1469 (fix/review-p0-p1-wave, head c8c463d22, MERGEABLE, 98 files,
  1 unresolved P1 review thread on src/agent/mcp_server.cpp:2663 — "Validate recording
  before starting the workflow"; 5 P2 threads on NON-R8 files).
- OPEN_ISSUE_MATRIX (R8-relevant): #1451 (unwired planes, partially fixed by #1469),
  #1452 (dock/copilot cluster; rubberBand P1 + llm re-entrancy fixed by #1469; 4 mcp_server
  P2 remain), #1454 (task-center cluster; P1 fixed by #1469; 4 P2 remain),
  #1459 (workflow/runtime cluster; 5 P2 remain), #1463 (journal/bench/recipes; 3 P2 remain),
  #1448 (fail-open cluster; mcp_server part addressed by #1469 but regressed into the
  review P1; workflow_run_coordinator terminal-save fixed by #1469).
- EXCLUDED_OWNERSHIP: experiment_store/teaching_admin parts of #1448 belong to other
  tracks; #1469 review P2s on model_runtime/plugin_registry/ipc_channel/radiometric_state/
  external_process are NOT R8 files.

## Oracle（可客观验证）
O1. `run_workflow` + recording: enable 失败时不得留有已分发且未记录的 workflow。
    顺序必须是 prepare/validate（不写实验行）→ dispatch → commit(enable)；
    commit 失败时对已接受 pipeline 做 typed 补偿（cancel），返回 typed refusal，
    retry 无重复副作用。基线红：master(+#1469 head) 上 enable 失败 → workflow 继续跑。
O2. MCP 分页: offset==sizeBytes 时不得返回 truncated=true/next_offset==size（EOF 死循环）。
O3. cancel-map 上限驱逐不得驱逐 in-flight 请求（对齐 progress map 的 evict-someone-else）。
O4. list_layers 等每层线性扫描 catalog → O(N*M)；改哈希/索引后 O(N+M)。
O5. stdin reader shutdown 不得走 terminate()（TerminateThread 危险路径）。
O6. output_committer: 注册失败时 temp 产物必须保留（temp-retention contract）。
O7. task_center pendingTaskUpdated 不得深拷整个 logBuffer（O(n²) 复制 + 无界内存）。
O8. local_worker_pool 不得跨线程驱动 QProcess（affinity 违例）。
O9. task_center 不得在锁外解引用裸 m_catalog（UAF 窗口）。
O10. workflow_runtime: !has_value() 分支不得解引用 optional（armed UB）；
     同步 fallback 判定不得依赖错误消息子串（typed failure）。
O11. workflow_run_coordinator: m_mutex 临界区内不得 QFile::remove（IO under lock）。
O12. scratch_registry digest 不得整文件读入（流式 1MiB 分块）。
O13. workflow_checkpoint archive 使用原子 rename（#1178 规则）。
O14. session_journal 驱逐不得丢弃 goal_normalization 记录（goal-consistency guard 失效）。
O15. agentbench workspace containment 不得是纯前缀比较（`/ws/../etc` 逃逸）。
O16. recipe_registry scan race 不得抛出未捕获 filesystem_error。
O17. 所有回归测试双跑通过；每个 fault cluster 独立 commit；rebase 后关键测试再双跑。
O18. 独立对抗 review 的 P0/P1 全部处置（修复或记录风险的 P2）。

## 与 #1469 的集成策略
#1469 未合并且拥有 mcp_server.cpp 的 handleRunWorkflow 邻域。本轨对 O1 做独立复现 +
在共享语义点（prepare→dispatch→commit/compensate）实现完整修复，兼容 #1469 已有的
"enable after submission gate" 意图（无孤儿行）。若 #1469 先合并，rebase 时按语义
对照保留本轨的握手实现（它是 #1469 修复的超集：无孤儿行 AND 无未记录分发）。

## 验证纪律
- 每个 cluster: 最小复现（failing test 或注入证据）→ 根因修复 → 针对性回归 ×2。
- 构建命令留痕: cmake -G Ninja, MSVC 2022, Qt 6.8.0.8, -j4 上限, CTest -j2。
- 禁止: 禁用测试、加大 timeout 掩盖、静默成功、盲刷 golden。
