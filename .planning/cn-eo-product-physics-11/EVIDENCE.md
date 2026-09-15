# EVIDENCE — cn-eo-product-physics-11

只记录本地可复现证据（命令 + exit + 关键输出摘要）。禁止引用在线 CI。

## Phase 0

- `git fetch origin --prune` → ok
- `git rev-parse origin/master` → `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
- `gh pr list --state open` → 仅 #1008（CONFLICTING）；#991/#992 已合入 master
- `gh issue list --state open` → #1001..#1007（dataset/workflow/georef/io:clip；与本 track 无交集）
- `git worktree add ../exp-rs-cn-eo-product-physics-11 -b zcode/cn-eo-product-physics-11 origin/master` → ok（12,362 files）
- 代码审计：见 BASELINE.md / CURRENT_ARCHITECTURE.md（文件:行号 证据在文中）
- 宿主资源测量方式：Windows `tasklist`（RSS）；Git Bash 下无 load average → 按协议记录一次 not-executed，`-j2` 恒定上限。

## 构建环境（Phase 1 发现与决策）

- 工具链探测：PATH 无 cmake/ninja；VS2022 Community 自带 cmake、ninja 在 `C:\Qt\Tools\Ninja\ninja.exe`；Qt 6.8.0 在 `C:\deps\Qt\6.8.0\msvc2022_64`；vcpkg 树 `C:\deps\vcpkg` + installed 树 `C:\Users\wangj.KEVIN\projects\exp-rs-win\build-win\vcpkg_installed`（README/scripts/windows/_env.cmd 记录的官方位置）。
- 首次 configure 失败：FetchContent 联网克隆 Catch2 v3.7.1 网络中断（HTTP/2 stream error，重试 3 次）。
- 处置：本机 `C:\deps\catch2-src` 恰为 v3.7.1（`git describe --tags` 核实）→ configure 追加 `-DFETCHCONTENT_SOURCE_DIR_CATCH2=C:/deps/catch2-src`（离线、同版本、不新增依赖、不改仓库）。第二次 configure exit 0，`build.ninja` 就绪。
- 资源上限：`CMAKE_BUILD_PARALLEL_LEVEL=2`、`ninja -j2`、test `ctest -j1`、`QT_QPA_PLATFORM=offscreen`；Git Bash 无 load average → 按协议记录 not-executed，`-j2` 恒定。冷构建期间 `tasklist` 观测 cl.exe RSS ≈ 140–300 MB/进程 ×2，远低于 70% 阈值。

## 构建资源记录（滚动）

| 事件 | 命令 | -j | 结果 | RSS 观测 |
|---|---|---|---|---|
| configure #1 | sicnu11_configure.cmd（setup.cmd 同参） | 2 | 失败：Catch2 克隆网络中断 | — |
| configure #2 | + FETCHCONTENT_SOURCE_DIR_CATCH2 本地 v3.7.1 | 2 | **exit 0** | — |

## OUT_OF_SCOPE

（Phase 2+ 发现时补充）
