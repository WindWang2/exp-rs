# DECISIONS — Track 14: CLI Surface Completion R4

## D1 — 退出码合同以仓库既有合同为准（提示词 0/2/3/4/5 分类作废）

任务书写作时的分类（0 成功/2 用法/3 数据/4 执行/5 环境）与仓库实际不符。实测 `src/sdk/exprs/exit_codes.h` 定义 8 值合同（0/1/2/3/4/5/6/7），且 `docs/headless/README.md:60-71` 已向用户发布同表。按"Phase 0 以实测为准"铁律：WP-A 逐命令审计对 **既有 8 值合同** 的符合度；每命令每类给出可触发用例或书面 N/A；`EXIT_CODE_CONTRACT.md` 记录 per-command 映射。任务书下限"每命令覆盖 2/3/4 三类之一"映射为仓库合同的 ValidationFailure(2)/ExecutionFailure(3)/MissingDependency(5)/InvalidInput(6) 等非零类的真实触发。**不发明新退出码值**。

## D2 — WP-C 词表审计重定版：GUI 词表轴让位 #1339，CLI 三级面独立成轴

实测 `data/help/commands.json`（76 条，`command.<id>`）是 GUI CommandRegistry（81 id）的帮助词表；shell 轴双向 diff 已由 `test_help_coverage`（既有）+ #1339（76→81 + schema 测试）关闭。本轨道不重做该轴。**CLI 词表面** = ①`docs/headless/README.md` 已发布命令清单 ↔ `isCliCommand`/dispatch（C-3 既有，扩展到子命令级）；②CLI 进程 help/usage 输出 ↔ 实际子命令/参数（双向）；③逐命令 usage 串 ↔ 实际参数表。固化测试读源/读文档/跑真进程三方对账。任务书"76 条目 diff 清零"在 CLI 轴面的落地：对 76 条逐条分类（CLI 相关/Gui-only）并给出 CLI 侧正确性证据，CLI 相关条目若与 CLI 表面冲突以实现真值修正对账文档。

## D3 — 账本随 PR 提交（"gitignored"注记与已跟踪现实冲突）

任务书注记账本 gitignored，但 `.goal-loop-ledger.md` 在 master 已是跟踪文件（#1336/#1340 先例均随 PR 提交），gitignore 对已跟踪文件无效且 `git rm --cached` 会侵害其它轨道。按仓库惯例处理：账本留在 worktree 根并随本分支提交（PR 证据链的一部分）。

## D4 — 新测试沿用 popen 真进程模式

`test_cli_commands_json.cpp` 确立 `SICNU_TEST_CLI` + popen + JSON 信封解析模式。本轨道新测试（退出码/错误信息/fail-fast/崩溃路径）一律断言**进程退出码 + stdout/stderr 公共表面**，不探针内部状态（任务书 TDD 纪律），与既有模式同源。

## D5 — 错误信息四元组落在 finish 出口层的结构化对象，不改信封既有字段语义

`CliIO::finish` 的 JSON 信封（ok/command/error/data/diagnostics/api_version）是已发布契约（test_cli_commands_json 断言其形状）。四元组结构化以**加法**方式落地：`error` 保持字符串（人读），新增结构化字段承载错误码/期望/实际/建议（机器读）；非 JSON 模式 stderr 行改为带错误码前缀的单行结构。既断言的既有字段不删除不改名（防 breaking）。

## D6 — 独立异常边界加在 dispatch 层（不加在 main）

`dispatchCliCommand` 是 19 命令的总入口，也是任务的"dispatch 层"定义点。兜底 catch 加在这里（每命令共享一处），映射到合同类 3（ExecutionFailure）或 5（RuntimeUnavailable）并输出结构化四元组；`main_cli.cpp` 不动（遗留 flag 面归 legacy 契约，本轨道不扩权）。

## D7 — `data/help/commands.json` 若必须改动：只动 CLI 轴相关条目，PR 正文声明与 #1336/#1337/#1339 的四方 rebase 时序

（依 Phase 2/3 实测重叠再决定是否触碰该文件；能不动就不动。）

## D8 — 编译资源与真重建

全程 `ninja -j2`（RSS>70% 降 `-j1` 有记录）、`CTEST_PARALLEL_LEVEL=1`。改完 TU 后删除对应 `.o` 再构建核对时间戳（raise-compiler-stack.sh 假绿陷阱防御，即使 gcc-15 下该脚本为 no-op 也执行此纪律）。关键验证双跑。
