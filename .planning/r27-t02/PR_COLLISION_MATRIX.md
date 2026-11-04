# PR_COLLISION_MATRIX — R27-T2

侦察时刻 2026-11-02T13:23Z：`gh pr list --state all` → **无 OPEN/草稿 PR**；最近合并 #1731 (R26)。无活跃远程分支与本轨目标文件冲突。

## 兄弟轨（同一 base `10652fd003`，同刻启动的 worktree）

| 轨 | worktree | 预期写入面 | 与本轨交叠风险 |
|---|---|---|---|
| R27-T1 science-numeric-quality | exp-rs-r27-t01-… | 科学算法/数值（src/science 相关、tests/test_r27_t01_*） | 低：路径不相交 |
| R27-T3 agent-security-sdk-plugins | exp-rs-r27-t03-… | src/agent、src/sdk、src/plugins、python | 低：路径不相交 |
| R27-T4（未观察到 worktree） | — | `src/app/dialogs/stac_browser_dialog.cpp`（#1765 GUI 侧） | 本轨不动 src/app/**；T4 消费本轨 `safeRemoteRasterPath` API |
| R27-T5（未观察到 worktree） | — | CI/构建共享资源 | 共享 `tests/CMakeLists.txt`、`.gitignore` append-only 段冲突 → 只重放自己追加段 |

## 共享受保护资源策略

- `tests/CMakeLists.txt`：仅追加带 `r27_t02` 唯一标识的注册块（append-only）；PR 前对 origin/master 三方检查。
- `.gitignore`：仅在文件末尾追加 `!.planning/r27-t02/` 白名单段。
- 根 `CMakeLists.txt`、`docs/adr/**`：不触碰。
- 本轨测试文件一律 `tests/test_r27_t02_*` 前缀。
