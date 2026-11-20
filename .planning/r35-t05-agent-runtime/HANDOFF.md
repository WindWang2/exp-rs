# HANDOFF — r35-t05-agent-runtime

## 给后续 T05 会话（本 PR 之后）

1. **proxy error 响应吞 body（R1 P3#4，既有行为非本 PR 引入）**：`worker_daemon.py` `_get_state`/`activeLayer` 匹配 `"result" in obj or "error" in obj` 后返回 `obj.get("result", {})`——匹配到的 JSON-RPC **error** body 被静默丢弃、以 `{}`/`None` 空结果呈现。建议后续将 error 转为可区分的 sentinel 或 stderr 记录（保持插件兼容前提下）。相关位点：两处 proxy 匹配 + 主循环 queued drain。
2. **WP03（Native 累计 bytes 预算）与 WP04（dock 批量渲染）**：未开工。`native_conversation_runtime.cpp` 累计 content/reasoning 无 cap；`onContentTokenReceived/onReasoningTokenReceived` 每 delta 全串 escape/setText。独立闭环，勿与本 PR 混合。
3. **WP06 Pi receipt readSmall**：stat 后 readAll 的增长文件/取消粒度边界未开工。
4. **真实 daemon 全进程腿**：本机 AF_UNIX listener 创建 EPERM（ENVIRONMENT_BLOCKED，研究阶段 `experience_daemon_probe.py` 已备）。环境允许时补 E2E-04 真实 socket 腿。
5. **在树内 fire-and-forget id（8002/8004/8005/9999）**：host 会对每个请求回 ACK（echo id）；任何新增相关调用必须用 `_next_proxy_request_id()`（>9999），新增 fire-and-forget 固定 id 也应避开 10000+ 域并回填 fresh-import 守卫测试。

## 给其他轨

- **T06**：本 PR 无新增 `tr()` 字符串，无 TS/staging 需求。
- **T02/T03/T04**：无接口变化；`SharedMemorySegment` wire/ABI 与 JSON-RPC 形状不变（R1 逐调用方核实）。

## 恢复检查点

- worktree：`/home/kevin/projects/rs-studio/exp-rs-r35-t05-agent-runtime-20261118T183744Z`（分支 `zcode/r35-t05-agent-runtime-20261118T183744Z`）
- build：`build-dev`（dev-default Debug，`-DOpenCV_DIR=/home/kevin/projects/rs-studio/main/.scratch/ui-build-deps/opencv/usr/lib/cmake/opencv5`）
- 无活动进程/锁；测试全绿基线见 ACCEPTANCE.md。
