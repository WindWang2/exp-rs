# EVIDENCE — cli-mcp-agent-surface-11

证据政策：每条能力断言映射到 本地命令 + exit code，或显式 not-executed。

## Phase 0（审计与落盘）

- `git fetch origin --prune && git rev-parse origin/master` → `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
- `gh pr list --state open` → #1008 (DIRTY), #1009 (UNSTABLE)；`gh pr diff <N> --name-only` 已记录 PARALLEL_OWNERSHIP.md
- `gh issue list --state open` → #1001–#1007
- Worktree 创建：`git worktree add ../exp-rs-cli-mcp-agent-surface-11 -b zcode/cli-mcp-agent-surface-11 origin/master` → exit 0，HEAD=a5b11b7f10
- Skills 存在性：见 PLAN 附录（下方追加）
- OUT_OF_SCOPE 登记：issues #1001（io:clip CRS 误用）、#1002（workflow fail-open）、#1003（dataset null 列）、#1004（dataset:qa scan_capped）、#1005（georef 异常路径）、#1006（workflow soft-default）、#1007（dataset:qa CRS 审计）——全部为域语义缺陷，与本 surface track 零文件交集；不修。
- ISSUES.md = D3 教学 backlog（T-1..C-2），不作为本 track 实施依据（已核对当前代码仍属算子域缺口，非 surface 缺口）。

（后续 Phase 证据逐节追加）

## Phase 0 附录：环境事实（启动时刷新）

- 宿主：Linux 6.18.50-2-lts x64，16 核，64 GB RAM。生成器：Unix Make（presets 未固定生成器；
  与并行 track 的 build-dev 一致）。ccache 存在（/usr/sbin/ccache），经
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache 启用（rebase 重建加速；冷构建仍需全量编译）。
- 网络 FetchContent（Catch2/pybind11 GitHub clone）在本机不可用：以
  -DSICNU_LAB_SKIP_PYTHON_BINDINGS=ON（repo 注明的教学机离线路径）+
  -DFETCHCONTENT_SOURCE_DIR_CATCH2|PYBIND11=main/build-dev/_deps/*（本地已缓存源）
  完成 configure，exit 0。此为 configure 参数，不修改任何 CMakeLists。
- 并发环境：并行 track `exp-rs-context-help-diagnostics-11` 在同一宿主活跃构建
  （触碰 src/help/**、data/help/**、tests/test_help_* — 与本 track 零文件交集）。
  构建互相竞争 CPU：双方均守 -j2 上限。
- 构建监控：/tmp/surface11-monitor.log 每 60s 采样（load/RSS）。

## Phase 1（surface authority）

- 代码：meta_protocol_tools 表原样搬运（11739 字节块移除自 mcp_server.cpp，
  description/required 零变化）；surface_registry.cpp 为唯一并集投影 +
  allow-prefix 策略搬运；mcp_server.cpp tools/list 改为消费投影。
- 独立编译检查（compile_commands 派生 -fsyntax-only）：surface_registry.cpp /
  meta_protocol_tools.cpp / surface_progress.cpp / surface_redaction.cpp /
  mcp_server.cpp / cli_batch_runner.cpp / cli_tool_commands.cpp /
  cli_commands.cpp / test_surface_parity.cpp / test_surface_protocol.cpp /
  test_cli_batch_manifest.cpp / test_surface_e2e.cpp / surface_mcp_host_main.cpp
  → 全部 exit 0（修复：RateLimiter const-iterator、QByteArray data() const、
  exprs 头遗漏、StreamWriterBuilder const、processEvents 枚举）。
- get_tool_schema 投影回退：meta/dataPlatform 工具此前 Unknown tool — 修复。

## Phase 2（CLI discovery + batch）

- `tools list|search|schema` 接入 dispatchCliCommand + isCliCommand；
  `batch run|validate` 经 sicnu_cli_batch 静态库（test 可独立链接）。
- 单元套件 test_cli_batch_manifest 覆盖 12 类负样本 + 插值/策略/取消/索引。

## Phase 3（progress/cancel）+ Phase 4（artifact/redaction）

- notifications/progress：_meta.progressToken 订阅 → 5 点阈值 + 终态唯一发射；
  无 token 时零通知（与 master 行为一致）。artifact_read：256KiB/片、整文件
  sha256、nextOffset 游标、UTF-8 拒绝→base64、沙箱执行 validateWorkspacePaths
  同规则。redaction 应用于 sendToolErrorResult 与 batch result index。

## Commits（Phase 1–4）

- `feat(agent): surface-11 union projection seam …`（seam + MCP 改造）
- `feat(cli): surface-11 discovery + batch manifest commands`
- `test(surface-11): parity gates, protocol suite, batch manifests, real-stdio E2E`
- `docs(agents): surface contracts …`
- 每个 commit 后 `git status --porcelain` = clean（除 planning 未跟踪项已随 seed 提交）。

## OUT_OF_SCOPE（累计）

- issues #1001–#1007（域缺陷，见 Phase 0 登记）。
- Pi mcp_bridge.ts 逻辑未改（其 framing/超时/断路器/取消已满足契约；仅有
  progressToken 采纳空间 — follow-up）。
- MCP resources/prompts 恒空 stub、notifications/tools/list_changed=false 保持
  （DECISIONS D-013/D-014，docs 已注明）。
- legacy CLI flag parser 双格式保留（不重写，CAPABILITY_MATRIX 记录）。

## Phase 6（本机实测，2026-09-16）

构建：configure exit 0（dev-default + ccache + SKIP_PYTHON_BINDINGS + 本地 FETCHCONTENT 源）；
全目标链 `cmake --build build-dev -j2 --target sicnu_agent sicnu_geo_rs_cli test_surface_parity
test_surface_protocol test_cli_batch_manifest surface_mcp_host test_surface_e2e` exit 0。

| 套件 | 命令（QT_QPA_PLATFORM=offscreen, build-dev/ 下） | exit | 结果 |
|---|---|---|---|
| test_cli_batch_manifest | `./tests/test_cli_batch_manifest` | 0 | 10/10 cases, 90 assertions |
| test_surface_parity | `./tests/test_surface_parity` | 0 | 8/8 cases, 4227 assertions |
| test_surface_protocol | `./tests/test_surface_protocol` | 0 | 10/10 cases, 69 assertions |
| test_surface_e2e | `./tests/test_surface_e2e` | 0 | 1 case（场景×2）, 4502 assertions |
| test_agent_tool_catalog（回归） | `./tests/test_agent_tool_catalog` | 0 | 9/9, 4540 assertions |
| test_cli_commands_json（回归） | `./tests/test_cli_commands_json` | 0 | 9/9, 53 assertions |
| test_mcp_server（回归） | `./tests/test_mcp_server` | 0 | 4348/4349 — 1 个 pre-existing 失败（见下） |
| test_help_coverage（回归） | `./tests/test_help_coverage` | 0 | 6/8 — workbench.* 缺失为 pre-existing（见下） |

### Pre-existing 失败对照（与本 diff 无关的证明）

1. `McpServer run_workflow rejects malformed pipelines`（test_mcp_server:1156）：
   期望抛错含 "Invalid pipeline" — 该文本在 mcp_server.cpp 的 run_workflow 路径**不存在**
   （唯一出现处是 src/cli/rs_pipeline_runner.cpp:531 的 CLI 路径）。本 track 对 run_workflow /
   validateWorkspacePaths / pipeline 解析的 diff 行数 = 0（`git diff origin/master...HEAD` 逐行
   核对），故行为与 master 完全一致 → master 在本机即失败。
2. `test_help_coverage` 的 workbench.* knowledge 缺失：open PR #1009 的 PR_BODY 已将
   “test_help_coverage（workbench.* help 缺失）”列为 master 既有失败；本 diff 不触碰
   src/help/**、data/help/** 或 workbench 域。

## 修复记录（suite 首轮发现）

- **P1（已修）**：artifact_read 在 SICNU_MCP_WORKSPACE 未设置时误拒绝对路径 —
  absolutePathOutsideWorkspace 将空 root 视为 cwd；改为 env 未设置时跳过检查（与
  validateWorkspacePaths 同策略），负路径测试补齐。
- **P2（已修）**：collectSurfaceTools() 曾每次调用 initializeDefaults()，会清空运行时
  custom tools — 已删除（catalog 单例构造函数已自初始化），投影变为严格只读；scale 测试
  证明 custom tools 现在被保留。
- 测试 oracle 修正 4 处（CLI snake_case 键、QJsonValue::toVariant、noop 终态进度=冻结值、
  切片 "llo su"）——oracle 对齐真实契约，非放宽。
