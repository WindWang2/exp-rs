# R32-T05: Agent dock lifecycle, Pi process deadlines, redaction semantics and SDK plugin packaging

> 下文保存原始 Linux 提交的范围与验证记录。后续安全复审取消了短凭据的长度豁免：所有非空凭据继续脱敏，碰撞协议身份时明确拒绝。#2080 中 `1` 等占位键的无条件会话/快照支持仍为 deferred；不应将其计为完整交付。最新 Windows 证据由合并 PR 的最终说明记录。

## 原问题与影响

R32 复核确认的 12 个 open 缺陷（#2073–#2080、#2106、#2107、#2109、#2111、#2112），影响同一 Qt 宿主下 Native/Pi Agent 的开始/审批/执行/取消/结束/保存/恢复闭环，以及插件包的安装与完整性校验：

- **Dock 生命周期**：无项目上下文时 `sendPrompt` 把 `beginRun` 失败误报为"另一 Agent 占用项目"并泄漏 pending prompt（#2073）；dock 与 View/Raster 服务持有裸指针且 `setContext` 拒绝被静默吞掉（#2074）；流式期项目切换覆写会话身份使 UI 永久卡在"Saving conversation"（#2075）。
- **PiProcess 状态机**：取消沿用 ≤3s 的传输关闭期限，必现 `shutdown_timeout`（#2076）；Starting 窗口内 close 与迟到 ready 竞态误报 handshake mismatch（#2077）；短 dummy apiKey（"1"/"none"）因子串碰撞使每次 checkpoint 保存必失败（#2080）。
- **审批与持久化**：无效 direct token 仍被旧 armed approval 兜底放行（#2078）；持久化 DecisionRecord 内层字段缺类型校验，Json::LogicError 可从 GUI 直调路径逃逸（#2079）。
- **插件与打包**：`exprs_plugin_host_worker` 无 install 规则，多配置 Windows staging 静默跳过（#2109）；SDK 路径经 ACP 解码损坏非 ASCII Windows 路径（#2111）；大写 hex 摘要被误判为不匹配（#2112）。
- **测试漂移**：CI 调用不存在的 `build/test_python_agent_mvp` 路径且分钟级用例缺 SLOW 标签（#2106）；r23_e dtype 探针断言错路径（#2107）；bilingual 交付测试 fixture 无法到达被测 UI 状态（#2081 测试侧）。

## 已合并基线

基于 master `53412c86`（#2061 后）。继承 #2063/#2066–#2072 已交付的 AgentToolHost 权威、Pi 宿主接入、完成态 checkpoint、无重放恢复与 Windows 中文路径修复；本 PR 只做上述确认缺陷的增量，不重复实施。

## 本次增量

（详见各 commit；每个修复均带回归测试）
1. `aab384a7d8` — #2076/#2077/#2080（PiProcess 期限/竞态/脱敏 + JS 协议侧 + `checkpoint-redaction` 测试）
2. `afca5d2023` — #2073/#2074/#2075（dock 拒绝语义/QPointer/延迟身份切换 + `test_r32_t05_agent_dock`）
3. `b87ddcd97a` — #2078/#2079（审批单次/typed 校验 + `test_r32_t05_ops_sdk`）
4. `d528ac3e36` — #2109/#2111/#2112（install 规则/UTF-8 路径构造/大小写摘要）
5. `580833562b` — #2106/#2107/#2081 测试侧（路径/标签/dtype/fixture）

## 边界与接口兼容

- `PiProcess::Config` 新增 `cancelTimeoutMs`（默认 30000，向后兼容）；`AgentToolHost::hasProjectContext()` 为新增只读查询。
- Pi 仍 opt-in，默认后端未动；未新增任何执行权威/资产系统/shell agent。
- `protocol.mjs` 导出 `redactCheckpoint`（内部函数转为可测导出）；≥8 字符真实密钥的脱敏严格性不变（既有 fixtureKey 用例保持）。
- 跨轨 HANDOFF：T06（.ts Base 上下文 + 新增 tr() 文案）、T02（同文件 rebase 注意）、T04（测试路径引用）。

## 实际验证（本机 Linux，Debug/dev-default，-j4 双锁，offscreen）

- 构建：受影响目标 19 个 → 0 编译错误（R1 独立审查以本树 compile DB 对全部改动 TU 做了语法编译）
- 新增回归：`r32_t05_dock::` 4/4、`r32_t05_ops::` 4/4 PASS（真实 Qt 对象 + 真实回环 SSE + 真实审批/注册表路径）
- 既有受影响套件：`test_agent_tool_host`/`conversation_store`/`test_agent_ops_core`/`r27t03`（approval gate）55/55；`test_exprs_plugin_system`/`loader`/`test_native_conversation_runtime`/`test_python_runtime_selection` 100%（1 例 Windows probe 按设计 skip）；`Conversation metrics` 4/4；`external process` 8/8；`test_agent_loop_core` 17 cases/216 assertions 全过（含 #2079）
- JS 协议（node --test）：`checkpoint-redaction` 4/4 PASS（#2080）；`protocol` 9 PASS/2 FAIL/4 SKIP（2 例需锁定 Windows SDK —— 维持 NOT_EXECUTED）
- Python：`test_r23_e_python_worker_typed.py` Ran 5 tests, OK（#2107 动态验证）
- #2109 install 树：`cmake --install` 后 `bin/exprs_plugin_host_worker` 存在（ELF、RPATH 自洽、ldd 0 缺失、可启动干净退出）；该目录的 `cmake_install.cmake` 从空变为含规则
- `test_pi_process`（真实 QProcess+协议 fixture）：Linux 下发现并修复 fixture 缺执行位（Windows CI 不需要，execve 需要；receipt 只哈希内容）后全量重跑
- 基线交叉判定：copilot_ui toolcard 用例在无本轨改动的 r31-T02 构建上同样失败 = 已有 issue #2130（stub 缺 producesFile 声明），按其期望修复

## 限制（诚实声明）

- 锁定 Pi SDK（node_modules）与 installed-entry lane：Linux 无 Windows 发行版，NOT_EXECUTED。`test_pi_process` 以真实 vendor recipes + 宿主 node 组成的自洽 fixture stage 运行（不降低任何断言）。
- 真实云模型、最终 Windows 发布验收：无授权环境，NOT_EXECUTED；不以模拟数字替代。
- #2081 的 zh_CN `.ts` Base 上下文归 T06；本 PR 修复该测试的 Agent fixture 侧。
- #2078 的内部状态差异（run() 的 post-hoc 诊断恒 non-repairable）无公共可观察面；行为测试钉住 typed 拒绝/一次性消费/正控制。

Refs #2073, Refs #2074, Refs #2075, Refs #2076, Refs #2077, Refs #2078, Refs #2079, Refs #2080, Refs #2081, Refs #2106, Refs #2107, Refs #2109, Refs #2111, Refs #2112

base=5ff15400047a6c55892669c9e9911e0e3b51bd04（二次 rebase：#2131/#2133/#2132 合入后；原分析基线 53412c86）
head=d206bb3cdc（11 commits；第二次 rebase 仅涉 .ps1 资源清单/CMakeLists 邻接上下文，生产代码与验证绑定的 85f6347785 逐字节一致——diff 复核）
review：R1@58083356 → 修复（QPointer 完整类型/override 名/MSVC path wrap/服务镜像）→ R2 SHIP@1033d1b1 → bilingual 权威版恢复 → R2 增量 SHIP@85f6347785

最终验证：pi_process 17/17、bilingual+r31_t02_core 8/8、copilot_ui 9/9、r32_t05 8/8、复跑 67/67（tool_host/ops/approval/plugin/external/metrics）；agent_loop_core 17/216；r23_e 5/5；JS redaction 4/4；install 树 worker 验证。0 失败（1 按设计 skip）。
