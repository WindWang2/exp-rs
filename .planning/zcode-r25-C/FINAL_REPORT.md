# FINAL_REPORT — R25-C

- **PR: https://github.com/WindWang2/exp-rs/pull/1727**（OPEN，非 draft，base=master）
- 分支 `agent/r25-C-pi-mcp-execution-20261028T140339Z`，6 commits，head `a5d0330611` 之后的 docs commit（PR body）为第 6 个。
- 基线：`origin/master` `2610cb4741e69a89124921e44c03e340e2522ee2`（执行时实时 fetch 确认；PR 前复查 master 未前进）。
- Worktree：`/home/kevin/projects/rs-studio/exp-rs-r25-C-20261028T140339Z`（源 worktree 未开发）。
- 合并完整性：`1065 targets / 50 add_test`，lost 0+0，added 2+0，exit 0；`git diff --check` 干净（唯一 trailing-whitespace 提示为 surface snapshot 生成器的既有 JSON 风格，master 同样存在；无冲突标记）。
- 与并行 PR #1724/#1725/#1726 的文件交集仅 `.gitignore` 与 `tests/CMakeLists.txt`（append-only 协议面），无源码重叠。

## 任务书 Oracle 对照（§7）
| Oracle | 状态 | 证据 |
|---|---|---|
| 1. ≥4 条真实 E2E 链（成功/拒绝/取消/恢复），真实 run_id、typed diagnostics | ✅ 5 链 | `pi/test/r25_c_e2e_real_host.test.mjs`：真 McpBridge + 真 surface_mcp_host，11–17s/轮，GREEN×2；48/48 pi 套件含它 |
| 2. ≥3 项功能深化且 schema 与调用端一致 | ✅ 3 项 | W1 桥接诚实化（类型码+代际漂移）、W2 能力条目诚实化、W3 run_id 恢复诊断；schema 变更走 snapshot 再生（diff 仅 get_workflow_status），pi/C++ 双侧钉住 |
| 3. 恶意输入矩阵 fail-closed + 旧缺陷保持绿 | ✅ | E2E 拒绝链（`../escape`、换行 id、未知 run）+ r22-agent 五项回归钉（URI 逃逸/env 注入/幽灵注册/IPC/JobEngine log：78+46+7+17+24031 断言） |
| 4. 非法输入不触发执行、幂等、资源回收 | ✅ | 拒绝在 dispatch 层先于执行；E2E 崩溃后 resume 走生产状态墙；孤儿进程回收经 R1/R2 修复（killNow 顺序 + SIGKILL 监听器）实证 0 残留 |
| 5. 有界内存/帧/竞态 oracle | ✅ | bridge 4MiB 出站/32MiB 入站上限保持（既有钉）；r18c 跨进程读写竞态修复（bounded poll）；E2E kill→resume 竞态以「stop+等待+bounded retry」闭合 |
| 6. pi/test + C 轨 test_r25_c_* + 既有回归 ×2 | ✅ | pi 48/48 ×2；C++ 见 TEST_RESULTS（198+95+9962+223+163+193+115+21+17866+r22 全绿，两轮） |
| 7. C/D 轨隔离 | ✅ | 零触碰 cartography/mapspec/layout/symbology/app/ui/operators/core/gui；ChartRegistry 未动 |

## 独立评审闭环
- R1（新上下文子代理）：P0=0 P1=0 P2=2 P3=9 → 全部修复（`41425da645`），回归全绿。
- R2（另一新上下文子代理）：P0=0 P1=0 P2=3 P3=7 → 全部修复（`a5d0330611`）；R2 实测 pi 46/46 与 C++ 二进制含修复代码；要求判别性证明后，本轨对两项修复做 revert 实证（各 1 失败）再恢复。

## 诚实限制
- 未等待线上 CI、未自动 merge（按协议）。
- GPU/在线 STAC 等本轨不涉及；无新增平台特异性代码（pi 测试沿用既有 Windows canary skip 约定）。
- `r18_c_id_null_error_fanout` 负载相关 flake：pristine master 2/8 复现、单跑 0/25 不复现，机制未定，记录不盲改。
- W5/W6：可解释闭环以「诊断/确认型」交付（W2 前置条件 + W3 恢复诊断），未建新自主 Agent 服务；插件 worker 的 E6xxx 类型化错误面已被 r23-E 覆盖，本轨以邻域回归维持（见 CROSS_TRACK_FINDINGS.md）。
- Issue #1640/#1650 无可复现代码缺口，未虚构修复（NOT_REPRODUCIBLE）。

## Loop 统计
6 个 loop 轮次（LOOP_LEDGER）；2 轮独立评审 + R1 前 1 次对抗预审；6 commits；pi 48 测试 ×2、C++ 14 个测试文件 ×2。
