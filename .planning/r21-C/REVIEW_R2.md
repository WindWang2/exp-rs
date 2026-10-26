# R21-C REVIEW_R2 — 第二轮独立对抗审查（fresh reviewer #2，R1 修复后全范围复审）

审查对象：R1 修复后的完整 diff（`911e0b17a3...HEAD`，20 commits）。审查方式：第二个 general-purpose 子代理，独立上下文；重点压测 R1 修复区（argv 重写、头文件 inline 化、黑名单门、有界读），含真实二进制对抗探针与 gdb 级复现思路。

## 发现与处置

| # | 级别 | 发现 | 处置 |
|---|---|---|---|
| R2-1 | **P2** | **F1 黑名单缺 revert-guard**：门终版是轮询黑名单，但其测试只经 `execute_algorithm` 提交——白名单与黑名单下都 armed，回退到白名单全套仍绿，而白名单会静默丢掉直连路由提交者（`view:`/`raster:`/allowed-id 经 dispatchToolCall）的取消通道 | **已修**（02ce1622ee）：新增 `rs:r18_c_sleep` 直连提交 + cancel 其 rpc id 的用例。**实证 RED**（临时回退白名单 → :308 失败 1 例）→ **GREEN**（恢复黑名单 → 13/3 全绿） |
| R2-2 | P3 | `--platform=` 附加形式只匹配单横线拼写 | **已修**（02ce1622ee）：`--platform=` 并入 attachedForm（Qt 本不消费该形式，属 belt-and-braces） |
| R2-3 | P3 | `workflow resume a b` 多余位置参数被忽略（与族"位置参数保留子命令语义"规则一致，注释略乐观） | 记录不改（行为正确：续的是 `a`） |
| R2-4 | P3 | `--now-ms/--ttl-ms` 对所有动作接受（仅 approve-repair 消费） | 记录不改（与族 action-agnostic 循环一致，next() 正确拒绝 flag 状值） |
| R2-5 | P3 | test_preflight_provider 写 5MiB 到 /tmp，tmpfs 满载时有假红风险 | 记录（本机 /tmp 32G tmpfs 曾被并行轨占满；有界读逻辑已按源码+直接 CLI 探针验证） |

## 审查确认无问题（压测项）
- **R1-1**（resume 双 takeFirst）：runId 仅消费一次；`resume no-such-run-r4` → exit 3 typed 非 134；`resume runA runB` 续 runA（修复前是 runB）；`--dry-runn` → 6；无参 → usage 6。
- **R1-2**（argv 重写）：off-by-one/null 终止正确；重复 flag、末位 token、附加形式全部无损坏；`dispatchCliCommand` 唯一生产调用方（main_cli.cpp:349）吃的是 originalArguments 派生路由——CLI 每种路由模式都看得到该 flag；零 exit-134（覆盖 `--platform=`/`-platform=`/`--platform x`/重复/末位全矩阵）。
- **R1-3**（inline 化）：GlobalFlags/takeGlobalFlag/extractGlobalFlags 仅 inline 于 cli_commands.h，无 ODR 冲突；cli_tool_commands 的 GlobalFlagsLocal 与本改动无耦合。
- **R1-4**（--now-ms/--ttl-ms）：`--now-ms --ttl-ms` → typed "needs an integer"（next() 不吃 flag 状值）。
- **R1-6**（mirror 有界读）：TOCTOU **fail-closed**——读仍以 min(size,cap)+1 为界，种植文件在任何分配前被拒；sizeEc 回退路径有界；symlink 跟随一致。
- **F1 门分支枚举**：get_execution_status/cancel_execution（黑名单✓）、get_workflow_status（execution_id 仅嵌套于 steps[]，天然不 arm✓）、execute_algorithm/execute_operator/run_workflow/resume_workflow + 直连分支（提交者，保留通道✓）。
- **反回滚**：R20 #1670 六文件零 diff 且门在位。**所有权**：全部路径在写区。**.gitignore**：4 行 append-only，合 r20-05 惯例。

## 结论
READY-WITH-FIXES → 唯一 P2 已修并实证 RED/GREEN；P3 按记录处置。**审查闭环完成**（R1 8 项 + R2 5 项全闭）。
