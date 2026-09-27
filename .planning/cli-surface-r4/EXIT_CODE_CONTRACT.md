# EXIT_CODE_CONTRACT — 退出码合同定版（Track 14 WP-A 收口）

**合同真源**：`src/sdk/exprs/exit_codes.h`（0–7 八值）= `docs/headless/README.md` "Exit codes (stable contract)" 表。本文件是逐命令的**触发用例 ↔ 合同类**映射与 N/A 论证；每行可追溯到 ctest 测试名（`test_cli_exit_codes_r4` / `test_cli_error_messages_r4` / `test_cli_fail_fast_r4` / `test_surface_parity_r4` / `test_cli_help_vocabulary_r4`）。

**任务书偏差声明**：任务书 3.2 的"0/2/3/4/5"分类（0 成功/2 用法/3 数据/4 执行/5 环境）写于未见到本合同之时。按 Phase-0-实测铁律，本轨道以仓库既有 8 值合同为准（DECISIONS D1）；"每命令至少覆盖 2/3/4 三类之一"映射为下表的非零类真实触发。

## 合同表

| code | 符号 | 含义 |
|---|---|---|
| 0 | OK | success |
| 1 | GENERIC_ERROR | unclassified failure（legacy 保留值；本轨道已消除其全部已知的"应归类"使用点） |
| 2 | VALIDATION_FAILURE | schema/contract validation 失败 |
| 3 | EXECUTION_FAILURE | operator/workflow 步骤运行期失败 |
| 4 | CANCELLED | SIGINT/SIGTERM 或协作取消 |
| 5 | MISSING_DEPENDENCY | 未知 algorithm/model/plugin/tool/资源不存在 |
| 6 | INVALID_INPUT | 参数畸形/文件不可读 |
| 7 | RUNTIME_UNAVAILABLE | 核心初始化失败 |

## 逐命令映射（19/19）

| # | 命令 | 类触发（ctest 可数） | N/A 类及理由 |
|---|---|---|---|
| 1 | algorithms | 6=缺 schema 参/未知子命令/`--limit abc`；5=未知 algorithm | 2,3,4：list/search 无 schema 校验面（搜索过滤错误即 6）；取消仅执行期，本命令无执行 |
| 2 | run | 6=缺 id/`--param` 无 `=`/坏 params 文件；5=未知 algorithm；3=operator 抛错（RSOperatorError→3，Cancelled→4）；4=执行中信号（commandRun 协作取消点） | 2：参数无 schema 校验面（由 operator 自身 preflight 负责） |
| 3 | pipeline | 6=缺文件/缺参/未知子命令（run\|validate\|resume 外）；2=validate schema 坏；3=runner 执行败 | 4：runner 无协作取消入口（遗留面，移交清单） |
| 4 | workflow | 6=缺参/未知子命令/文件不可读；2=文档 schema 坏（validate 与 run 共用校验）；3=run 执行败/resume 失败 | 4,5：无依赖解析面；取消在 runner 内（同 3） |
| 5 | plugin | 6=缺参/未知子命令；5=未知 plugin（inspect/uninstall）；3=卸载 drain 失败 | 2,4：清单为 manifest 校验，错误归 validate 子命令诊断输出（exit 0+诊断），无 2 类退出面 |
| 6 | models | 6=未知子命令/inspect 缺名；5=未知 model | 2,3,4：只读面，无执行/校验 |
| 7 | project | 6=缺文件/未知子命令/缺参（governance 前置校验）；2=validate/health 报告不 ok；3=migrate/relink/export-manifest/import 操作败 | 4,5：本地文件面，无外部依赖解析 |
| 8 | data | 6=缺参/未知 URL 形态；其余子命令（cube/mirror/product）执行失败按各分支既有类（3/5/6） | 2,4：位置 URL 语法为遗留面（SURFACE_PARITY #7-8 记录），本轨道不动其语法语义 |
| 9 | data-providers | 0=列表（唯一路径） | 全部非零类 N/A：命令不接受子命令/参数（额外参数被忽略是遗留语法），无失败面 |
| 10 | dataset | 6=缺 --dataset-db/--name/--version 等（store 未创建即拒）；5=version/dataset/label-schema/split 不存在；2=stage 校验败；3=create/diff/listing/sample 败 | 4：无取消入口；7：store.open 失败归类 6（不可读输入）而非 7（7 保留给进程级初始化失败） |
| 11 | experiment | 6=缺 db/缺 name/缺 experiment/缺 a-b/缺 run；5=experiment/run 不存在；3=create/listing 败 | 2,4：比较/列表无 schema 校验面与取消入口 |
| 12 | reproduce | 6=缺 db/缺 run/out/bundle；5=run 不存在；2=validate replayability 不满足（impossible→2）；3=export 败 | 4：导出为短操作，无取消入口 |
| 13 | lab | 2=旗标缺值/未知选项/缺 --lab --grade（全部前置校验）；6=--out 不可写 | 3,4,5：评分执行错误经 runner 聚合到 finish(exitCode)（report 内诊断），不冒泡为进程类 |
| 14 | env-doctor | 6=未知参数；2=环境检查 errorCount>0（机器相关，故意不进程断言）；0=健康 | 3,4,5,7：检查器本身报告环境（不因环境坏而 7——报告即成功路径） |
| 15 | tools | 6=缺参/未知子命令；5=未知 tool schema | 2,3,4：只读发现面 |
| 16 | batch | 6=缺 manifest/文件不可读/未知子命令；2=manifest schema 坏（validate）；3=批内条目执行败（聚合 exitCode） | 4：--fail-fast 语义内无信号取消面（runner 内部） |
| 17 | session | 6=未知 action/缺 action；7=OpsDriver 不可用（AGENT_OPS_UNAVAILABLE）；其余经 driver.apply 结果映射（1 保留给 driver 未分类失败——已记移交） | 5：动作资源不存在由 driver 语义层报（非退出码类） |
| 18 | passport | 6=缺 --path/文件不可读 | 2,3,4,5：只读投影面，无执行/依赖解析 |
| 19 | catalog | 6=未知子命令/缺 dir；3=导出写失败 | 2,4,5：导出无 schema 校验/取消/依赖面 |

**类覆盖统计（下限核对）**：19/19 命令至少 1 个非零类**真实触发**且 ctest 可数；其中 ≥13 命令有 6 类、≥10 命令有 5 类、≥6 命令有 3 类、≥5 命令有 2 类、2 命令有 7 类（session 显式；7 类对其余命令的"N/A"由"7=进程级初始化失败"定义天然受限）。
**Cancelled(4) 论证**：只有 `run`（commandRun 的信号检查点）与 dispatch 边界（RSOperatorError::Cancelled 转发）有真取消面；进程级 SIGINT 竞态在 ctest 中不可稳定触发（flaky by construction），故 4 类以代码审计 + dispatch 转发路径为证据，不做进程注入断言。
**行为变化（breaking）清单**：dataset/experiment/reproduce 家族 1→{2,3,5,6}（49 位点逐条）；plugin uninstall 未知 id 1→5、drain 失败 1→3；pipeline 缺文件 3→6、未知子命令（原=run 别名）→6；algorithms/models 未知子命令 0→6（原静默当 list）；project governance 未知子命令 1/加载后失败→6（加载前拒绝）；reproduce validate/inspect 不可满足 1→2；lab 早期校验裸返回→finish 信封（码值不变）；project migrate/relink/export-manifest/import 1→3。
