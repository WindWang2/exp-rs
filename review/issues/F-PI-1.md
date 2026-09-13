# [Pi/Bridge] MCP 桥行缓冲溢出后不拆失步流——僵尸桥使后续全部工具调用 10 分钟超时

P2
Affected Location: pi/mcp_bridge.ts:176-198（onStdout 溢出分支），同缺陷复制于 pi/exp-rs-spatial.ts:202-224
Root Cause & Impact: >32MiB 的输出意味着 stdio JSON-RPC 帧已失步（服务端自身行上限 4MiB）。溢出分支只清 buffer 并拒绝 pending，不 kill 子进程、不置 exited——lazy-respawn 条件 (!this.child || this.exited) 永不触发。无终止换行的超长输出会把后续每个响应粘连成永不解析的行：每个工具调用等满 10 分钟超时，无诊断指向失步，唯一恢复路径是重载扩展。subagent V 精化：以换行结尾的有界超长行可在下一行边界自愈；僵尸态特指无终止换行/持续洪泛。
Reproduction: pi/test/mcp_bridge.test.mjs 增补——fake server 写 33MiB 无 \n 数据后再写正常响应；断言后续 request() 正常 resolve（现实现挂起）。
Recommended Fix: 溢出分支走 TooLarge/协议失败同路径：kill 子进程（下一次 request 触发 lazy-respawn）或显式 exited=true + child=null 后 respawn。
