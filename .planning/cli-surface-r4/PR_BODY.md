# PR BODY DRAFT — hardening/r4-cli-surface（Phase 6 提交时以实际数据回填）

标题：`fix(cli): surface completion R4 — classify every command's exit codes (published 0..7 contract), structured four-tuple errors, README vocabulary bidirectional gate`

> 标题说明：任务建议的标题是"CLI 表面完善：退出码、错误信息与三面一致性"，本 PR 即其内容——19 个顶层命令对既有 0..7 退出码合同的逐命令符合、命令级错误的结构化四元组、以及已发布词表的双向对账门。未新增命令/算子/功能方向。

## 一、基线与范围

| 项 | 实测值 |
|---|---|
| 基线 `origin/master` | `15e5c66b54`（PR #1333 合并点，开分支时实测未前进） |
| 本分支领先/落后 | `+37 / -0`（`git rev-list --left-right --count origin/master...HEAD`） |
| 原子提交 | 37 个（每个独立可编译，静态验证 + 全量过滤面 ctest 门） |
| 开放 issue | 0 |
| 工作树 | `exp-rs-cli-surface-r4`（独立 worktree，master 只读） |
| 构建资源 | 全程 `ninja -j2`（RSS 远低于 70%，无 -j1 降级）；`CTEST_PARALLEL_LEVEL=1`；gcc-15（16.2.1 ICE 规避 + launcher 显式置空） |
| 本地验证 | 已验证、未等待线上 CI（任务纪律 ci=none）——双跑日志回填 EVIDENCE §4 |

## 二、与在途 PR 的文件重叠

- `src/cli/rs_pipeline_runner.cpp`（#1334 沙箱）：**本分支零 diff**——containment 语义只消费不修改。
- `tests/CMakeLists.txt`（#1334/#1335/#1339/#1340 均触碰）：本分支的注册 hunk 集中放在 `test_cli_commands_json` 块之后的文件尾区域，纯追加，rebase 可机械收敛。
- `data/help/commands.json`：**本分支零 diff**——实测其为 GUI CommandRegistry 词表（81 轴归 #1339），CLI 词表面走 `docs/headless/README.md`。
- `docs/headless/README.md`：新增 session 命令行 + 错误消息格式段（任务书允许的两段之一；另一段退出码合同已存在）。
- `.gitignore`：一行白名单例外 `.planning/cli-surface-r4/*.md`（RS14-16 先例同款）。

## 三、逐 WP 根因与修复

- **WP-A 退出码合同（定版 + 19/19）**：合同真源实测为 `src/sdk/exprs/exit_codes.h`（0..7，README 已发布）——任务书 0/2/3/4/5 分类作废（DECISIONS D1）。逐命令审计发现三层分化：模范（run/workflow/pipeline…）、混合（project_ops 5 处裸 1）、违约（dataset 家族 fail() 恒 1；algorithms/models 未知子命令静默当 list 退 0）。修复：49+ 位点分类（dataset 24/experiment 11/reproduce 8/…），reproduce 二值返回归类；pipeline 缺文件 3→6；plugin uninstall 未知 id→5；全部见 `EXIT_CODE_CONTRACT.md` 19 行 + N/A 论证 + breaking 清单。
- **WP-B 错误四元组（≥20 条路径）**：`finish()` 加法参数 `CliErrorDetails`（JSON 增 error_details；文本行升级 `[E-n:SYMBOL] msg; expected:…; actual:…; hint:…`，缺省参数=逐字节旧行为）。每 TU 单一构造点（usageError/fail/labError/passportError/sessionError），共 110+ 位点；无散拼错误串。
- **WP-C 词表对账（CLI 三级面）**：`test_cli_help_vocabulary_r4` 双向（docs backtick 首词 ⊆ CLI 拒绝信息；dataset/experiment/reproduce 的 expected 词表与 README **精确集合相等**；session 14 动词专测）。README 补 session 行（18→19 行，修 master 既有红 C-3）。`data/help/commands.json` 不属 CLI 面（实测 GUI 词表，D2）。
- **WP-D 三面一致（16 操作）**：`SURFACE_PARITY.md` 16 行 + 共真源锚（AtomicAlgorithmRegistry/DatasetStore/WorkflowRunCoordinator+TaskCenter）+ 12 条 CLI 侧自动断言；MCP/GUI 侧实现零触碰（3 处 MCP 侧缺口移交）。
- **WP-E fail-fast（≥12 用例）**：dataset/experiment/reproduce 动词与必需旗标在 store.open（SQLITE_OPEN_CREATE）**之前**校验——副作用零断言（store 文件不存在）入测；algorithms/models/pipeline 未知子命令、run 未知旗标 parse 期拒绝。
- **WP-F 崩溃路径**：dispatch 层加外层异常边界（main 无 catch——此前未捕获异常直达 SIGABRT），三级 catch 全走四元组出口；18 处既有 catch 点审计（零静默 0）。
- **WP-G 防回归**：5 个新 ctest 目标；`COMMAND_MATRIX.md` 19/19 逐行可追溯。

## 四、退出码合同表

见 `docs/headless/README.md`（0..7）与 `.planning/cli-surface-r4/EXIT_CODE_CONTRACT.md`（逐命令映射 + N/A 论证）。

## 五、19/19 覆盖总表

见 `.planning/cli-surface-r4/COMMAND_MATRIX.md`。

## 六、词表 diff 清零证据

`ctest -R cli_help_vocabulary`（回填输出）：19 行命令表、方向 1（15 探针 SECTION）、方向 2（3 精确集合）、session 专测。

## 七、用户可感知行为变化（breaking，逐条）

1. dataset/experiment/reproduce 家族失败退出码 1→{2,3,5,6}（逐位点见 EXIT_CODE_CONTRACT breaking 清单）。
2. `pipeline run <缺文件>` 3→6；`pipeline <未知子命令>`（原=run 别名）→6。
3. `algorithms/models <未知子命令>`：原静默执行 list 退 0 → 6。
4. `plugin uninstall <未知id>` 1→5；drain 失败 1→3。
5. project migrate/relink/export-manifest/import 操作失败 1→3；未知 governance 子命令在加载工程**前**拒绝（6）。
6. reproduce validate/inspect 不可满足 1→2。
7. 错误文本模式新增 `[E-n:SYMBOL]` 前缀与 expected/actual/hint 段；JSON 信封新增 error_details 对象（既有字段不变）。
8. session usage 行补列 clear_pause/clear_cancel（本就被接受）。

## 八、未解决项（移交）

1. MCP data-platform 工具错误无结构化 code（src/agent 白名单外）→ agent-MCP 面 track。
2. CLI workflow resume 缺 run_id 字符集 parse 期校验（MCP 有）→ 下轮。
3. `data` 位置 URL 遗留语法保持（记录于 SURFACE_PARITY）。
4. GUI 81 词表轴归 #1339。

## 九、验证

- `ctest -R "cli|exit_code|help" -j1` 连续两轮：36 tests，97% passed（35/36），两轮结果逐位一致。
- 唯一红 = `test_help_coverage`"Shell command ids all have help knowledge"——**master 既有红**，5 个缺失 id 与 open PR #1336 修复清单逐一吻合（GUI 词表轴归 #1336/#1339，本分支不越界重修）；相对基线零新增失败。
- 本分支 5 个新套件 27 用例 / 601 断言全部在册通过；test_cli_command_surface 的 C-3 红（README 缺 session 行）已由本分支修复。
- 附带修复的 master 潜伏缺陷：`-name/--name` argv 对被 QgsApplication 平台层消费（dataset/experiment `--name` 从不工作）；sicnu_agent 缺 agent_loop 链接（4 个测试目标无法链接）。
