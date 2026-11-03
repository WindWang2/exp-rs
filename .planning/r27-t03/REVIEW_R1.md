# R27-T3 REVIEW_R1（独立对抗评审，general-purpose 子代理，隔离视角）

- 范围：git diff 10652fd003..HEAD（实现时点 7bd49daf61），四视角：安全红线/并发内存/API 契约/测试强度
- 判定：**REJECT**（1×P0 功能无效 + 1×P1 barrier 竞态 + P2/P3 若干）

## FINDINGS 与处置

| # | 级别 | 发现 | 处置 |
|---|---|---|---|
| F1 | P0/S0 | copilot 门只扫 envelope 顶层 `parameters` 键，而真实 wire 形状是 OpenAI `{function:{name,arguments}}` → **门对所有生产调用 no-op**；测试用了自造形状（green-but-broken）；审批卡也读了不存在的键 | **已修**：门改为 `gateCallArguments(dispatcher.argumentsFor(envelope))`；header 增加 `envelopeArguments`（理解 function/parameters/arguments/params 四形）+ `decodeStringArguments`（JSON 字符串 blob 解码后扫描）；审批卡用同一 parsed args；新增真实 OpenAI 形状 + 字符串 blob 测试（拒绝/写审批两向） |
| F2 | P1 | dock executor 出队后才置 mWorkerRunning=true，joinWorker 谓词可在窗口内误判 idle（barrier 契约可破） | **已修**：置位移入出队临界区 |
| F3 | P2 | consume 时环满会逐出仍存活的已消费条目（有界重放窗；需 30min 内 17 次人工审批才可达） | **已修**：容量 16→64 + consume 满员 fail-closed（APPROVAL_REPLAY_RING_FULL，本次无审批放行、门再问；不再静默逐出）；arm 满员本已拒绝；测试按容量访问器填充 |
| F4 | P2 | #1798 无 C++ 测试；resumeRun 从不置 invalidInput（退出码映射死代码） | **已修**：ctest 脚本直测 CLI 二进制四形状退出码=6（r27_t03_cli_invalid_input.cmake）；resumeRun 未知 run id 置 invalidInput |
| F5 | P2 | 门潜在漏报：字符串编码参数 blob 不解码扫描；写槽清单偏窄 | **已修**（随 F1）：blob 解码后扫描；写槽补 target/filename/save_file；free-text 子树豁免与 MCP 一致（保留，注释不再 overclaim） |
| F6 | P3 | 脱敏 Slack 前缀用了空格形（xoxb␣）而非真实 token 的连字符形 | **已修**：xoxb-/xoxa-/xoxp-/xoxs-/xapp-/xoxe- |
| F7 | P3 | 设备 driver 拒绝表仅 gpsbabel；env 族缺 GCONV_PATH/PERLLIB/JAVA_TOOL_OPTIONS/JDK_JAVA_OPTIONS/BASH_FUNC_ 前缀 | env **已修**（含 BASH_FUNC_ 前缀 ShellShock 类）；设备表按段扫描为真正缓解，保持 gpsbabel 显式 + 注明构造性上限（记录不扩） |
| F8 | P3 | mintRepairApprovalForOperator const+注释线程契约无强制 | 记录：现仅 dock executor 调用（grep 证实），MCP/CLI 为独立进程；保持 const + 强化注释 |
| F9 | P3 | canonical 绑定分隔歧义（findingsDigest 含 `\|` 理论碰撞）；MAC 比较非常数时间 | 记录：digest 来源为 sha256Hex（16 hex），不可达；本地进程内表面，常数时间收益≈0 |
| F10 | P3 | 明文 Bearer 守卫只认 localhost/127.0.0.1/::1，127/8 其余与映射形误拒（可用性） | **已修**：QHostAddress::isLoopback() 全回环判定 |

## 评审确认的关键正面结论（摘）
- HMAC 核心与独立 python 实现重算一致（KAT 0844e5f3b72315f1 ✓）；K 常数/padding/ipad-opad/>64B key 全对
- ring 不可被模型灌满（失败 token 不入环）；过期逐出谓词与 verify 过期谓词严格互补
- 秘钥从未序列化（grep）；除 run 中继 token（HMAC 校验）外无 wire 动作可达审批状态
- dock 析构/取消协议/QueuedConnection 生命周期可靠（除 F2）
- #1741 段扫描对评审自构造形状无新绕过；`..` 反斜杠形死于 weaklyCanonicalPath fail-closed
- worker -32001 为合法 JSON-RPC server error，宿主通用处理（无 wire 破坏）；sys._getframe(1) 对仓库内全部调用形态正确
- tests/CMakeLists 目标名 base→HEAD 严格只增（7 个新目标，零删除）——重建未丢任何测试
