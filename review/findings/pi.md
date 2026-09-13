# Findings — pi/ (TS bridge layer)

## F-PI-1 · 行缓冲溢出后不拆失步流——桥变僵尸，后续全部工具调用超时

- **Severity**: P2
- **Lens**: 5（协议契约）+ 3（状态同步）
- **Location**: `pi/mcp_bridge.ts:176-198`（onStdout）、同缺陷复制于 `pi/exp-rs-spatial.ts:202-224`
- **Code**:
  ```ts
    private onStdout(chunk: string): void {
      this.buffer += chunk;
      if (this.buffer.length > MAX_LINE_BUFFER_CHARS) {
        // Fail fast instead of growing without bound on a runaway server.
        this.buffer = "";
        const err = new Error(
          `exp-rs MCP bridge: response line exceeded ${MAX_LINE_BUFFER_CHARS} chars`,
        );
        for (const p of this.pending.values()) {
          clearTimeout(p.timer);
          p.reject(err);
        }
        this.pending.clear();
        return;
      }
  ```
- **Root cause**: >32 MiB 的"行"意味着 stdio 的 JSON-RPC 帧已经失步（服务器自己的行上限是 4 MiB，正常数据到不了 32 MiB）。fail-fast 只清空本地 buffer 并拒绝 pending；子进程保持存活，失步的字节流继续作为后续响应被 JSON.parse 丢弃。之后每个新请求写入正常，但响应永远解析不出来 → 10 分钟超时/次，且 lazy-respawn 分支不触发（`exited` 仍为 false）。
- **Trigger condition**: 服务端输出**不带换行的**超长异常字节（崩溃前的内存垃圾、stdout 被第三方库污染）或持续刷屏——无终止换行符时失步尾部把后续每个响应粘连成永不解析的行（subagent V 精化：以换行结尾的有界超长行会在下一行边界自愈，onLine 丢弃非 JSON 行即可恢复；僵尸态特指无终止换行/持续洪泛）。
- **Impact**: 桥永久失效但表现为"每个工具调用超时 10 分钟"，无诊断指向失步；唯一的恢复路径是重载扩展。
- **Evidence**: `static-only`（逻辑封闭，可由 fake server 注入 >32MiB 无换行输出验证）
- **Reproduction**: `pi/test/mcp_bridge.test.mjs` 增补用例：fake server 写入 33 MiB 无 `\n` 数据后写正常响应 `{"jsonrpc":"2.0","id":1,...}`；断言后续 request() 能在正常超时内 resolve。现实现 pending 清空后新请求永远等不到解析成功的行。
- **Recommended fix**: 溢出分支与 `TooLarge`/协议失败同路径处理：`this.child?.kill()`（下一次 request 走 lazy-respawn）或显式 `this.exited = true; this.child = null` 后触发 respawn。
- **Dedupe**: new——#645/#623/#669/#706 覆盖泄漏/取消/respawn 循环，未覆盖失步后不拆流。

## F-PI-2 · exp-rs-spatial.ts 缺少 startup-deadline 的成功路径清理——事件循环被 30s 定时器钉住（修复未从 mcp_bridge.ts 回移）

- **Severity**: P2
- **Lens**: 5（同源实现漂移）+ 2（资源）
- **Location**: `pi/exp-rs-spatial.ts:178-198`（spawn 的 deadline 段）；对照已修复的 `pi/mcp_bridge.ts:144-172`
- **Code**:
  ```ts
  // pi/exp-rs-spatial.ts:178-198 —— 没有 finally 清理：
      const startupDeadline = new Promise<never>((_, reject) => {
        const t = setTimeout(
          () =>
            reject(
              new Error(
                `exp-rs MCP server did not initialize within ${STARTUP_TIMEOUT_MS / 1000}s`,
              ),
            ),
          STARTUP_TIMEOUT_MS,
        );
        spawnFailure.catch(() => {}).finally(() => clearTimeout(t));
      });
      await Promise.race([
        this.request("initialize", { ... }),
        spawnFailure,
        startupDeadline,
      ]);
      this.notify("notifications/initialized", {});
  ```
  ```ts
  // pi/mcp_bridge.ts:158-172 —— 同一段代码的修复版（含注释说明该 bug）：
      try {
        await Promise.race([ ... ]);
      } finally {
        // Clear on EVERY settle path: a healthy initialize used to leave the
        // 30s timer armed, pinning the event loop after every spawn/respawn.
        cancelStartupDeadline();
      }
  ```
- **Root cause**: mcp_bridge.ts 从 exp-rs-spatial.ts 抽出时修掉了"健康 initialize 之后 30s 定时器仍 armed"的问题，但修复（try/finally + cancelStartupDeadline）没有回移到原文件。两个文件是活跃分叉（都有各自的小修复），修复漂移未被任何一致性测试钉住。
- **Impact**: 每次 spawn/respawn 后事件循环被钉住最长 30 秒：Pi 宿主进程在空闲后延迟退出；respawn 频繁的会话中退避延迟叠加。无正确性影响，但用户可见（扩展卸载/进程退出挂起）。
- **Trigger condition**: 任何一次成功的 `initialize`（即每次正常 spawn）。
- **Evidence**: `static-only`（Node.js timer ref 语义，可用 `process._getActiveHandles()` 或退出计时验证）
- **Reproduction**: 启动 Pi 扩展并完成一次 spawn，记录进程空闲后的退出延迟；现实现 ≤30s，修复版即退。或单测：spawn 后 35s 内 `process.exit` 被延迟。
- **Recommended fix**: 把 mcp_bridge.ts:158-172 的 try/finally 形态回移到 exp-rs-spatial.ts；长期应删除分叉（exp-rs-spatial.ts 引用 mcp_bridge.ts），并加一条防漂移测试。
- **Dedupe**: new——#645 修的是 exit 监听器堆叠，#669 修的是 respawn 循环；本条是修复单向回移缺失。
