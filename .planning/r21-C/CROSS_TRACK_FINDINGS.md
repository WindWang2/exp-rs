# R21-C CROSS_TRACK_FINDINGS — 跨轨发现与协调记录

## XT-1 [E 轨 / verification] 共享 stash 误pop事故与恢复（2026-10-24，R21-C 操作失误，已恢复）
- **事件**：R21-C 执行 `git stash pop`（意图弹出自己的 `r21c-src-fixes`）时，`refs/stash` 在所有 worktree 间共享，另一条轨在 R21-C 入栈之后又入栈一笔（hash `382d6a8d6`，含 `src/agent/CMakeLists.txt`、`src/agent/output_verifier.{cpp,h}`、`src/verify_adapters/CMakeLists.txt`，+79 行，属 verification/E 轨写区）。`pop` 取了 `stash@{0}`=对方那笔并 drop 了它，改动落到 R21-C 工作树。
- **恢复**：R21-C 随即将该 4 文件原样重新入栈（`stash@{0}: RECOVERED-BY-R21-C: ...`，消息含原 hash、文件清单与归属说明），随后正确弹出自己的 `stash@{1}`（r21c-src-fixes，hash 8512afac3a，内容 6 文件 +222/-22 完整恢复）。对方轨改动内容零丢失。
- **给各轨的操作要求**：`git stash list` 是全仓共享视图；pop 前必须 `git stash show -p 'stash@{N}'` 或按 hash `git stash pop <hash>` 精确指定，禁止裸 `git stash pop`。R21-C 后续一律用 hash 精确弹出。
- **待办**：E/verification 轨 owner 认领 `stash@{0}`（RECOVERED-BY-R21-C）并应用到自己的工作树。

## XT-2 [主机环境] vendor 前缀 libgdal 的 wrapper2 LAPACK 符号全主机无提供者 → link-shim 模式（R21-B/R19-02 同款）
- vendor `libgdal.so` 未定义 `wrapper2_dposv_` 等 26 个符号；其 DT_NEEDED `libarmadillo.so.15` 在 paleo vendor 前缀不存在（仅存于 pw-seismic 前缀，且那个也不导出 wrapper2）。
- 既有解法（R19-02/R21-B 已用）：`build/link-shim/`（libopenblas.so.0 提供 26 个 wrapper2_* no-op 转发桩 + libarmadillo/libarpack 小桩 + libblas/liblapack/libpcre2 → /usr/lib 符号链接）+ `CMAKE_EXE_LINKER_FLAGS="-L<shim> -Wl,--allow-shlib-undefined"`。
- 运行期还需 `LD_LIBRARY_PATH=<shim>:<vendor-prefix>`（DT_RUNPATH 不传递）与 `PROJ_DATA=<vendor>/share/proj`、`GDAL_DATA=<vendor>/share/gdal`（否则 harness_evals 等族假红）。
- R21-B 当前二进制同样无法直接运行（libgeos.so.3.14.1 解析失败，RUNPATH 相同）——建议其按同 recipe 补 link-shim 与环境变量。R21-C 的完整配方已写入 `.planning/r21-C/BASELINE.md`。

## XT-3 [04/D 轨] CT-6 存续确认
- 7 个 cartography 工具 raw schema 缺项 → test_surface_snapshot registry 腿红（R19-02 记录，R20-03 复核）。本轨保持 KNOWN_BASELINE_FAIL 证据新鲜；属 D 轨写区，不抢修。

## XT-4 [共享灰区] 无新发现
- 本轨改动未触碰根 CMakeLists/tests/CMakeLists/data 注册表；共享灰区零冲突。
