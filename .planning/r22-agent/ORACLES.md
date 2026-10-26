# R22-04 (r22-agent) ORACLES

每个工作包的可客观验证完成条件；全部要求 RED（master 行为复现）→ GREEN（修复）→ GREEN×2（双跑一致）。

## D0a — #1704
- RED：`pathOutsideWorkspace("CSV:"+工作区外真实csv, ws)` 在 master 返回 false（放行）；`WMS:http://169.254.169.254/…`、`PG:host=…`、`GPKG:/outside/x.gpkg:layer`、`NETCDF:"/outside/x.nc":var` 同样放行（test_r22_agent_containment 断言在 master 全部失败）。
- GREEN：上述全部拒绝；`CSV:`+工作区内相对/绝对路径仍放行；`data:a.tif`/`./data:a.tif` 相对名不回归；`C:/x`、`C:` 保持 LocalPath；SICNU_MCP_ALLOW_REMOTE=1 时网络驱动与 http 一致放行。
- 退出码：`ctest -R test_r22_agent_containment` 0。

## D0b — #1690
- RED：environment key `"LD_PRELOAD=/tmp/x.so "` → master started=1（实锤复现于 issue 评论）。
- GREEN：`LD_PRELOAD=… `/`ld_preload=… `/`A=B`/空 key/NUL key 全部 E5007 拒绝；合法 key（R22_PROBE/GDAL_HTTP_TIMEOUT）仍到达子进程；manifest 解析期拒绝非法 env key（含 `1BAD`/`BAD-NAME`/`BAD.NAME`）。
- 退出码：`ctest -R test_r22_agent_env_keys` 0。

## D1a — #1691
- RED：注册后 sleep(8)>5s 超时的插件在 loadPlugin 返回 nullptr 后 `AtomicAlgorithmRegistry::findAdapter("py:ghost_slow_register") != nullptr`（幽灵残留）。
- GREEN：失败后 findAdapter == nullptr；unloadAll 后仍为空。
- 退出码：`ctest -R test_r22_agent_ghost_algo` 0（RUN_SERIAL，真 daemon）。

## D1b — #1692
- RED：>1MiB 单行响应 → 回调不触发（5s 窗口超时证明挂起），pending 仍可 take。
- GREEN：回调 ≤5s 内以 isError=true + message 含 "exceeds" 触发；pending 不再含该 id；后续短行正常；未知 id 的超长行保持静默丢弃。
- daemon：`python3 -m py_compile` 通过；>1MiB 响应改发 -32001 typed error。
- 退出码：`ctest -R test_r22_agent_ipc` 0。

## D2a — #1705
- RED：25000 行 logInfo → snapshot logLines.size()==25002（master 无界）。
- GREEN：size ≤ 10000 且 size+droppedLogLines==25002；front/back 内容精确；delta 流（offset,size）严格连续、无重复；1MiB 单行被截断含 "[truncated"；40 行短任务全量保留。
- 退出码：`ctest -R test_r22_agent_job_logs` 0。

## D2b — #1693
- ① StdinReader 死分支删除（编译级 + 标准库语义论证，P3，无独立可执行 oracle；评审确认）。
- ② 终态后日志回调早退（代码级守卫；P3）。

## 全轨
- 关键回归（containment/env/ghost/ipc/job_logs 五目标）×2 遍退出码一致；
- 受影响既有族复跑：test_mcp_server、test_r14_C_external_process_env、test_exprs_external_process、test_job_engine、test_python_plugin_host、test_plugin_host；
- `git diff --check` 干净；文件白名单核对通过。
